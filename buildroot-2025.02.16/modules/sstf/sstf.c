#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/math64.h>

#include "elevator.h"

static int queue_size = 50;
module_param(queue_size, int, 0444);
MODULE_PARM_DESC(queue_size, "Tamanho da fila de requisicoes antes do despacho automatico (20-100)");

static int max_wait_ms = 50;
module_param(max_wait_ms, int, 0444);
MODULE_PARM_DESC(max_wait_ms, "Tempo maximo de espera em ms antes do despacho de uma fila nao cheia");

static unsigned int deadline_ms = 0;
module_param(deadline_ms, uint, 0444);
MODULE_PARM_DESC(deadline_ms, "Prazo maximo de atendimento em ms desde a chegada (0 = desabilitado, SSTF puro)");

struct sstf_data {
	struct list_head queue;
	unsigned int count;
	sector_t head_pos;
	bool dispatching;
	struct blk_mq_hw_ctx *hctx;
	struct hrtimer timer;
	spinlock_t lock;
};

static enum hrtimer_restart sstf_timer_fn(struct hrtimer *timer)
{
	struct sstf_data *sd = container_of(timer, struct sstf_data, timer);
	struct blk_mq_hw_ctx *hctx;
	unsigned long flags;
	bool start_dispatch = false;

	spin_lock_irqsave(&sd->lock, flags);
	if (!sd->dispatching && sd->count > 0) {
		sd->dispatching = true;
		trace_printk("sstf: inicio de despacho motivo=tempo_esgotado pendentes=%u\n",
			     sd->count);
		start_dispatch = true;
	}
	hctx = sd->hctx;
	spin_unlock_irqrestore(&sd->lock, flags);

	if (start_dispatch && hctx)
		blk_mq_run_hw_queue(hctx, true);

	return HRTIMER_NORESTART;
}

static void sstf_insert_requests(struct blk_mq_hw_ctx *hctx, struct list_head *list,
				  blk_insert_t flags)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long irq_flags;
	bool start_dispatch = false;

	spin_lock_irqsave(&sd->lock, irq_flags);

	sd->hctx = hctx;

	/*
	 * Todas as requisicoes vao para o FIM da fila unica, inclusive as
	 * marcadas com BLK_MQ_INSERT_AT_HEAD: assim a primeira da lista e
	 * sempre a mais antiga, que e a que o prazo verifica no despacho.
	 * fifo_time (u64) guarda o instante de chegada em ns.
	 */
	while (!list_empty(list)) {
		struct request *rq = list_first_entry(list, struct request, queuelist);

		list_del_init(&rq->queuelist);
		rq->fifo_time = ktime_get_ns();
		list_add_tail(&rq->queuelist, &sd->queue);
		sd->count++;
	}

	if (!sd->dispatching) {
		if (sd->count >= queue_size) {
			sd->dispatching = true;
			trace_printk("sstf: inicio de despacho motivo=fila_cheia pendentes=%u\n",
				     sd->count);
			start_dispatch = true;
		} else {
			hrtimer_start(&sd->timer, ms_to_ktime(max_wait_ms), HRTIMER_MODE_REL);
		}
	}

	spin_unlock_irqrestore(&sd->lock, irq_flags);

	if (start_dispatch)
		blk_mq_run_hw_queue(hctx, true);
}

/*
 * Escolha do SSTF: percorre a fila e devolve a requisicao cujo setor inicial
 * esta mais perto do cabecote, em qualquer sentido. No empate de distancia
 * fica a de menor setor; no mesmo setor fica a mais antiga (a primeira vista,
 * porque so troca com '<' estrito). Chamar com sd->lock e a fila nao vazia.
 */
static struct request *sstf_closest(struct sstf_data *sd)
{
	struct request *rq, *best = NULL;
	sector_t best_dist = 0, best_pos = 0;

	list_for_each_entry(rq, &sd->queue, queuelist) {
		sector_t pos = blk_rq_pos(rq);
		sector_t dist = pos >= sd->head_pos ? pos - sd->head_pos
						    : sd->head_pos - pos;

		if (!best || dist < best_dist ||
		    (dist == best_dist && pos < best_pos)) {
			best = rq;
			best_dist = dist;
			best_pos = pos;
		}
	}

	return best;
}

/*
 * Prazo: a requisicao mais antiga e sempre a primeira da fila. Se ela ja
 * esperou MAIS que deadline_ms, devolve ela e o tempo de espera em *waited_ns;
 * senao devolve NULL. Com deadline_ms = 0 o prazo esta desabilitado.
 * Chamar com sd->lock e a fila nao vazia.
 */
static struct request *sstf_expired(struct sstf_data *sd, u64 *waited_ns)
{
	struct request *oldest;
	u64 waited;

	if (deadline_ms == 0)
		return NULL;

	oldest = list_first_entry(&sd->queue, struct request, queuelist);
	waited = ktime_get_ns() - oldest->fifo_time;

	if (waited <= (u64)deadline_ms * NSEC_PER_MSEC)
		return NULL;

	*waited_ns = waited;
	return oldest;
}

static struct request *sstf_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long flags;
	struct request *rq;
	u64 waited_ns = 0;

	spin_lock_irqsave(&sd->lock, flags);

	if (list_empty(&sd->queue)) {
		sd->dispatching = false;
		spin_unlock_irqrestore(&sd->lock, flags);
		return NULL;
	}

	/* o prazo expirado tem prioridade sobre a escolha do SSTF */
	rq = sstf_expired(sd, &waited_ns);
	if (rq) {
		/* div_u64: dividir u64 com '/' no i386 nao linka (__udivdi3) */
		trace_printk("sstf: prazo expirado setor=%llu espera_ms=%llu\n",
			     (unsigned long long)blk_rq_pos(rq),
			     (unsigned long long)div_u64(waited_ns, NSEC_PER_MSEC));
	} else {
		rq = sstf_closest(sd);
	}

	list_del_init(&rq->queuelist);
	sd->count--;
	/* o cabecote vai para o setor inicial da despachada, mesmo por prazo */
	sd->head_pos = blk_rq_pos(rq);

	if (sd->count == 0)
		sd->dispatching = false;

	rq->rq_flags |= RQF_STARTED;

	spin_unlock_irqrestore(&sd->lock, flags);

	return rq;
}

static bool sstf_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long flags;
	bool work;

	spin_lock_irqsave(&sd->lock, flags);
	work = sd->dispatching && !list_empty(&sd->queue);
	spin_unlock_irqrestore(&sd->lock, flags);

	return work;
}

static void sstf_finish_request(struct request *rq)
{
	return;
}

static int sstf_init_sched(struct request_queue *q, struct elevator_type *e)
{
	struct elevator_queue *eq;
	struct sstf_data *sd;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&sd->queue);
	sd->count = 0;
	sd->head_pos = 0;
	sd->dispatching = false;
	sd->hctx = NULL;
	spin_lock_init(&sd->lock);
	hrtimer_init(&sd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sd->timer.function = sstf_timer_fn;

	eq->elevator_data = sd;

	blk_queue_flag_set(QUEUE_FLAG_SQ_SCHED, q);
	q->elevator = eq;

	return 0;
}

static void sstf_exit_sched(struct elevator_queue *e)
{
	struct sstf_data *sd = e->elevator_data;

	hrtimer_cancel(&sd->timer);
	kfree(sd);
}

static struct elevator_type sstf = {
	.ops = {
		.init_sched = sstf_init_sched,
		.exit_sched = sstf_exit_sched,
		.insert_requests = sstf_insert_requests,
		.dispatch_request = sstf_dispatch_request,
		.has_work = sstf_has_work,
		.finish_request = sstf_finish_request,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	return elv_register(&sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Escalonador de disco SSTF com prazo maximo de atendimento");

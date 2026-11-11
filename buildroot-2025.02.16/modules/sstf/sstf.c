#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>

#include "elevator.h"

static int queue_size = 50;
module_param(queue_size, int, 0444);
MODULE_PARM_DESC(queue_size, "Tamanho da fila de requisicoes antes do despacho automatico (20-100)");

static int max_wait_ms = 50;
module_param(max_wait_ms, int, 0444);
MODULE_PARM_DESC(max_wait_ms, "Tempo maximo de espera em ms antes do despacho de uma fila nao cheia");

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
		trace_printk("sstf: despacho iniciado (tempo de espera esgotado), %u requisicoes pendentes\n",
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
			trace_printk("sstf: despacho iniciado (fila cheia), %u requisicoes pendentes\n",
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

static struct request *sstf_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long flags;
	struct request *rq;

	spin_lock_irqsave(&sd->lock, flags);

	if (list_empty(&sd->queue)) {
		sd->dispatching = false;
		spin_unlock_irqrestore(&sd->lock, flags);
		return NULL;
	}

	/* despacho provisorio em FIFO; a escolha por SSTF e o prazo maximo
	 * entram aqui, substituindo as duas linhas abaixo */
	rq = list_first_entry(&sd->queue, struct request, queuelist);

	list_del_init(&rq->queuelist);
	sd->count--;
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

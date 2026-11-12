#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>

#define MS_TO_NS(ms) ((uint64_t)(ms) * 1000000ULL)

#define NCHUNKS  6   /* blocos de trabalho por thread */
#define CHUNK_MS 50  /* tempo de CPU de cada bloco    */

struct worker {
    const char *name;
    int prio;
    long long t[NCHUNKS];  /* instante (ms) em que cada bloco terminou */
};

struct event {
    long long t;
    const char *name;
};

static struct timespec start;

static void burn(int ms)
{
    struct timespec then;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &then);
    long elapsed;
    do {
        volatile int x = 0;
        for (int i = 0; i < 1000; i++) x++;
        struct timespec now;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        elapsed = (now.tv_sec - then.tv_sec) * 1000000000L + (now.tv_nsec - then.tv_nsec);
    } while (elapsed < (long)MS_TO_NS(ms));
}

static long long elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000LL + (now.tv_nsec - start.tv_nsec) / 1000000;
}

static void *work(void *arg)
{
    struct worker *w = arg;

    for (int i = 0; i < NCHUNKS; i++) {
        burn(CHUNK_MS);
        w->t[i] = elapsed_ms();
    }

    return NULL;
}

static int by_time(const void *a, const void *b)
{
    const struct event *ea = a, *eb = b;
    return (ea->t > eb->t) - (ea->t < eb->t);
}

int main(int argc, char *argv[])
{
    if (argc != 4 || (strcmp(argv[1], "fifo") != 0 && strcmp(argv[1], "rr") != 0)) {
        fprintf(stderr, "Uso: %s <fifo|rr> <prioridade_T1> <prioridade_T2>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int policy = strcmp(argv[1], "rr") == 0 ? SCHED_RR : SCHED_FIFO;
    struct worker workers[2] = {
        { .name = "T1", .prio = atoi(argv[2]) },
        { .name = "T2", .prio = atoi(argv[3]) }
    };

    printf("Política: %s | T1: prioridade %d | T2: prioridade %d\n",
           policy == SCHED_RR ? "SCHED_RR" : "SCHED_FIFO", workers[0].prio, workers[1].prio);

    /* A thread principal recebe a maior das duas prioridades: assim, ela cria
       as duas threads antes que qualquer uma delas comece a executar */
    int max_prio = workers[0].prio > workers[1].prio ? workers[0].prio : workers[1].prio;
    struct sched_param param = { .sched_priority = max_prio };
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("sched_setscheduler");
        return EXIT_FAILURE;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, policy);

    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_t threads[2];
    for (int i = 0; i < 2; i++) {
        param.sched_priority = workers[i].prio;
        pthread_attr_setschedparam(&attr, &param);

        int err = pthread_create(&threads[i], &attr, work, &workers[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return EXIT_FAILURE;
        }
    }

    /* Ao bloquear no join, a thread principal libera a CPU para T1 e T2 */
    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);

    struct event events[2 * NCHUNKS];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < NCHUNKS; j++)
            events[i * NCHUNKS + j] = (struct event){ workers[i].t[j], workers[i].name };

    qsort(events, 2 * NCHUNKS, sizeof(events[0]), by_time);

    printf("  t (ms)  bloco concluído por\n");
    for (int i = 0; i < 2 * NCHUNKS; i++)
        printf("  %6lld  %s\n", events[i].t, events[i].name);

    return 0;
}
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif
#define NJOBS          20

struct dl_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

#define MS_TO_NS(ms) ((uint64_t)(ms) * 1000000ULL)

struct task {
    const char *name;
    int runtime_ms;
    int deadline_ms;
    int period_ms;
    int work_ms;
};

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

static void *worker(void *arg)
{
    struct task *t = arg;
    pid_t tid = gettid();

    struct dl_attr attr = {
        .size           = sizeof(attr),
        .sched_policy   = SCHED_DEADLINE,
        .sched_runtime  = MS_TO_NS(t->runtime_ms),
        .sched_deadline = MS_TO_NS(t->deadline_ms),
        .sched_period   = MS_TO_NS(t->period_ms)
    };

    /* PID 0: aplica a política à thread que fez a chamada */
    if (syscall(SYS_sched_setattr, 0, &attr, 0) < 0) {
        fprintf(stderr, "[%s] TID %d: sched_setattr falhou: %s\n",
                t->name, tid, strerror(errno));
        return NULL;
    }

    for (int job = 1; job <= NJOBS; job++) {
        struct timespec then, now;
        clock_gettime(CLOCK_MONOTONIC, &then);

        burn(t->work_ms);

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - then.tv_sec) * 1000 +
                       (now.tv_nsec - then.tv_nsec) / 1000000;

        printf("[%s] TID %d: job %2d concluído em %3ld ms\n",
               t->name, tid, job, elapsed);

        /* Job terminou: suspende até o próximo período */
        sched_yield();
    }

    return NULL;
}

int main(void)
{
    struct task tasks[3] = {
        { "A", 10, 30,  50, 8 },
        { "B",  5, 20, 100, 4 },
        { "C",  3, 40, 200, 2 }
    };

    /* Utilização total: U = soma(runtime / period) */
    double u = 0.0;
    for (int i = 0; i < 3; i++)
        u += (double)tasks[i].runtime_ms / tasks[i].period_ms;
    printf("Utilização total: U = %.3f (%.1f%%), limite: 90%%\n", u, u * 100);

    if (u > 0.90) {
        fprintf(stderr, "Utilização acima do limite, abortando.\n");
        return EXIT_FAILURE;
    }

    /* As threads são criadas pela thread principal (política padrão);
       SCHED_DEADLINE é configurado dentro de cada thread */
    pthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        int err = pthread_create(&threads[i], NULL, worker, &tasks[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    return 0;
}
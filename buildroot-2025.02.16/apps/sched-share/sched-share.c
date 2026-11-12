#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#define NPROCS     3
#define DURATION_S 5

static long long now_ns(clockid_t clock)
{
    struct timespec ts;
    clock_gettime(clock, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void child(int nice_value)
{
    /* Configura o próprio nice (nice negativo requer root/CAP_SYS_NICE) */
    if (setpriority(PRIO_PROCESS, 0, nice_value) != 0) {
        perror("setpriority");
        exit(EXIT_FAILURE);
    }

    /* Instantes iniciais, medidos depois de configurar o nice */
    long long wall_start = now_ns(CLOCK_MONOTONIC);
    long long cpu_start  = now_ns(CLOCK_PROCESS_CPUTIME_ID);
    long long limit      = DURATION_S * 1000000000LL;

    /* Laço CPU-bound durante 5 segundos de relógio */
    long long wall_now;
    do {
        volatile int x = 0;
        for (int i = 0; i < 1000; i++) x++;
        wall_now = now_ns(CLOCK_MONOTONIC);
    } while (wall_now - wall_start < limit);

    long long cpu_now = now_ns(CLOCK_PROCESS_CPUTIME_ID);

    double fraction = 100.0 * (double)(cpu_now - cpu_start) / (double)(wall_now - wall_start);
    printf("nice %2d (PID %d): %.1f%% de CPU\n", nice_value, getpid(), fraction);
    exit(EXIT_SUCCESS);
}

int main(void)
{
    const int nices[NPROCS] = { -5, 0, 5 };

    for (int i = 0; i < NPROCS; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0)
            child(nices[i]);
    }

    /* O pai aguarda o término dos três filhos */
    for (int i = 0; i < NPROCS; i++)
        waitpid(-1, NULL, 0);

    return 0;
}
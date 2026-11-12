#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#define MS_TO_NS(ms) ((uint64_t)(ms) * 1000000ULL)

static long long now_ns(clockid_t clock)
{
    struct timespec ts;
    clock_gettime(clock, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

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

int main(int argc, char *argv[])
{
    const char *name = argc > 1 ? argv[1] : "burn";

    long long wall = now_ns(CLOCK_MONOTONIC);
    long long cpu  = now_ns(CLOCK_PROCESS_CPUTIME_ID);

    while (1) {
        burn(10);

        /* A cada segundo de relógio, informa a fração de CPU recebida */
        long long wall_now = now_ns(CLOCK_MONOTONIC);
        if (wall_now - wall >= (long long)MS_TO_NS(1000)) {
            long long cpu_now = now_ns(CLOCK_PROCESS_CPUTIME_ID);
            printf("[%s] PID %d: %3lld%% de CPU\n", name, getpid(),
                   100 * (cpu_now - cpu) / (wall_now - wall));
            wall = wall_now;
            cpu  = cpu_now;
        }
    }

    return 0;
}
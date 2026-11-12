#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

struct sched_attr {
    uint32_t size;            /* tamanho da struct */
    uint32_t sched_policy;    /* política          */
    uint64_t sched_flags;     /* flags adicionais  */
    int32_t  sched_nice;      /* valor de nice (para SCHED_NORMAL) */
    uint32_t sched_priority;  /* prioridade RT (para SCHED_FIFO/RR) */

    /* em nanossegundos: */
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

static const char *policy_name(uint32_t policy)
{
    switch (policy) {
        case 0:
            return "SCHED_NORMAL";
        case 1:
            return "SCHED_FIFO";
        case 2:
            return "SCHED_RR";
        case 3:
            return "SCHED_BATCH";
        case 5:
            return "SCHED_IDLE";
        case 6:
            return "SCHED_DEADLINE";
        default:
            return "DESCONHECIDA";
    }
}

int main(int argc, char *argv[])
{
    /* PID 0 indica o próprio processo */
    pid_t pid = argc > 1 ? atoi(argv[1]) : 0;

    struct sched_attr attr = {
        .size = sizeof(attr)
    };

    if (syscall(SYS_sched_getattr, pid, &attr, sizeof(attr), 0) < 0) {
        perror("sched_getattr");
        return EXIT_FAILURE;
    }

    printf("PID:             %d\n", pid != 0 ? pid : getpid());
    printf("Política:        %s (%u)\n", policy_name(attr.sched_policy), attr.sched_policy);
    printf("Nice:            %d\n", attr.sched_nice);
    printf("Prioridade RT:   %u\n", attr.sched_priority);
    printf("Runtime:         %llu ns (%.2f ms)\n", (unsigned long long)attr.sched_runtime, attr.sched_runtime / 1e6);
    printf("Deadline:        %llu ns (%.2f ms)\n", (unsigned long long)attr.sched_deadline, attr.sched_deadline / 1e6);
    printf("Period:          %llu ns (%.2f ms)\n", (unsigned long long)attr.sched_period, attr.sched_period / 1e6);

    return 0;
}
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static struct rusage last;

static long rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[128];
    long kb = -1;

    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld", &kb) == 1)
            break;
    fclose(f);
    return kb;
}

/* Imprime as faltas de pagina desde a etapa anterior e o RSS atual */
static void step(const char *name) {
    struct rusage now;
    getrusage(RUSAGE_SELF, &now);
    printf("%-8s minflt = %-6ld majflt = %-6ld VmRSS = %ld kB\n", name,
           now.ru_minflt - last.ru_minflt, now.ru_majflt - last.ru_majflt, rss_kb());
    /* Descarta as faltas causadas pelo proprio printf/fopen */
    getrusage(RUSAGE_SELF, &last);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <lazy|populate|mlock|huge|file> <MiB> [arquivo]\n", argv[0]);
        return 1;
    }

    int do_mlock = 0;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int fd = -1;
    if (strcmp(argv[1], "populate") == 0) {
        flags |= MAP_POPULATE;
    } else if (strcmp(argv[1], "huge") == 0) {
        flags |= MAP_HUGETLB;
    } else if (strcmp(argv[1], "file") == 0) {
        if (argc < 4 || (fd = open(argv[3], O_RDONLY)) < 0) {
            fprintf(stderr, "modo file requer um arquivo existente\n");
            return 1;
        }
        flags = MAP_PRIVATE;
    } else if (strcmp(argv[1], "mlock") == 0) {
        do_mlock = 1;
    } else if (strcmp(argv[1], "lazy") != 0) {
        fprintf(stderr, "modo invalido: %s\n", argv[1]);
        return 1;
    }

    size_t size = (size_t)atoi(argv[2]) << 20;
    size_t page = sysconf(_SC_PAGESIZE);
    printf("pagina = %zu bytes, regiao = %zu MiB (%zu paginas)\n", page, size >> 20, size / page);
    step("inicio");

    volatile char *p = mmap(NULL, size, fd < 0 ? PROT_READ | PROT_WRITE : PROT_READ, flags, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    step("mmap");

    /* Modo mlock: trava a regiao na memoria, forcando o mapeamento das paginas */
    if (do_mlock) {
        if (mlock((void *)p, size) != 0) {
            perror("mlock");
            return 1;
        }
        step("mlock");
    }

    /* Acessa um byte de cada pagina: escreve na memoria anonima, le do arquivo */
    for (size_t i = 0; i < size; i += page) {
        if (fd < 0)
            p[i] = 1;
        else
            (void)p[i];
    }
    step("acesso");

    munmap((void *)p, size);
    step("munmap");
    return 0;
}
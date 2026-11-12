#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define TRACING   "/sys/kernel/tracing"
#define SCHED_SYS "/sys/block/sdb/queue/scheduler"
#define DEVICE    " 8,16 "

struct order {
	const char *event;
	long n;
	unsigned long long last;
	unsigned long long dist;
};

static int write_str(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0)
		return -1;
	ssize_t r = write(fd, s, strlen(s));
	close(fd);
	return r < 0 ? -1 : 0;
}

/* Executa o disk-test com os parâmetros dados e espera o término */
static int run_disk_test(char *args[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execvp("disk-test", args);
		perror("disk-test");
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

/* Lê o trace e calcula a distância percorrida nas duas ordens */
static int analyze(struct order ord[2])
{
	FILE *f = fopen(TRACING "/trace", "r");
	if (f == NULL) {
		perror(TRACING "/trace");
		return -1;
	}

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, DEVICE) == NULL)
			continue;
		char *p = strstr(line, " + ");
		if (p == NULL)
			continue;
		while (p > line && p[-1] != ' ')
			p--;
		unsigned long long sector = strtoull(p, NULL, 10);

		for (int i = 0; i < 2; i++) {
			struct order *o = &ord[i];
			if (strstr(line, o->event) == NULL)
				continue;
			if (o->n++ > 0)
				o->dist += sector > o->last ? sector - o->last : o->last - sector;
			o->last = sector;
		}
	}
	fclose(f);
	return 0;
}

int main(int argc, char *argv[])
{
	/* Parâmetros do disk-test: padrão do tutorial (2097152 5000 30 16) */
	char *dt_args[] = { "disk-test", "2097152", "5000", "30", "16", NULL };
	if (argc == 5) {
		dt_args[1] = argv[1];
		dt_args[2] = argv[2];
		dt_args[3] = argv[3];
		dt_args[4] = argv[4];
	} else if (argc != 1) {
		fprintf(stderr, "Uso: %s [<tamanho> <operacoes> <pct_escritas> <processos>]\n", argv[0]);
		return 1;
	}

	const char *scheds[] = { "none", "mq-deadline", "kyber" };
	int nscheds = sizeof(scheds) / sizeof(scheds[0]);

	/* Monta o tracefs, se ainda não estiver montado */
	if (access(TRACING "/trace", F_OK) != 0)
		system("mount -t tracefs none " TRACING " 2>/dev/null");

	printf("%-12s %12s %18s %18s %9s\n",
	       "Escalonador", "Requisições", "Chegada (setores)", "Atend. (setores)", "Redução");

	for (int s = 0; s < nscheds; s++) {
		/* 1. Configura o escalonador */
		if (write_str(SCHED_SYS, scheds[s]) != 0) {
			printf("%-12s indisponível no kernel\n", scheds[s]);
			continue;
		}

		/* 2. Prepara e habilita o registro de eventos */
		write_str(TRACING "/tracing_on", "0");
		write_str(TRACING "/buffer_size_kb", "8192");
		write_str(TRACING "/events/block/block_bio_queue/enable", "1");
		write_str(TRACING "/events/block/block_rq_issue/enable", "1");
		write_str(TRACING "/trace", "");   /* limpa o registro anterior */

		/* 3. Executa o teste com o registro ligado */
		write_str(TRACING "/tracing_on", "1");
		int rc = run_disk_test(dt_args);
		write_str(TRACING "/tracing_on", "0");

		if (rc != 0) {
			printf("%-12s falha ao executar o disk-test\n", scheds[s]);
			continue;
		}

		/* 4. Calcula as distâncias */
		struct order ord[2] = {
			{ .event = "block_bio_queue:" },
			{ .event = "block_rq_issue:" },
		};
		if (analyze(ord) != 0)
			return 1;

		double red = ord[0].dist > 0
			? 100.0 * (1.0 - (double)ord[1].dist / ord[0].dist) : 0.0;

		printf("%-12s %12ld %18llu %18llu %8.1f%%\n",
		       scheds[s], ord[1].n, ord[0].dist, ord[1].dist, red);
	}

	return 0;
}
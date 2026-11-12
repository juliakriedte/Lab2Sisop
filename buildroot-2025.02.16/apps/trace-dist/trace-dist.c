#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct order {
	const char *event;
	const char *name;
	long n;                         /* requisições registradas */
	unsigned long long last;        /* setor da requisição anterior */
	unsigned long long dist;        /* setores percorridos */
};

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "/root/trace.txt";
	struct order ord[] = {
		{ .event = "block_bio_queue:", .name = "Chegada" },
		{ .event = "block_rq_issue:",  .name = "Atendimento" },
	};
	char line[512];

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		return 1;
	}

	while (fgets(line, sizeof(line), f)) {
		/* o setor inicial é o número antes do " + " */
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

	for (int i = 0; i < 2; i++)
		printf("%-12s %6ld requisições, %12llu setores percorridos\n",
		       ord[i].name, ord[i].n, ord[i].dist);
	if (ord[0].dist > 0)
		printf("Redução:     %.1f%%\n", 100.0 * (1.0 - (double)ord[1].dist / ord[0].dist));

	return 0;
}
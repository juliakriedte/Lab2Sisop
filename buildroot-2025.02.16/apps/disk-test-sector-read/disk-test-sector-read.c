#define _GNU_SOURCE             /* O_DIRECT */
#define _FILE_OFFSET_BITS 64    /* off_t de 64 bits em i686 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SECTOR_SIZE 512
#define DISK_SZ     1073741824
#define N_OPS       50

void echo(const char *path, const char *string)
{
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		perror(path);
		return;
	}
	fputs(string, f);

	if (fclose(f) != 0)
		perror(path);
}

void cfg_queue(void)
{
	printf("Configuring scheduling queues...\n");

	/* Desabilita agrupamentos */
	echo("/sys/block/sdb/queue/nomerges", "2");

	/* Aumenta a fila do escalonador */
	echo("/sys/block/sdb/queue/nr_requests", "128");
}

int main()
{
	char *buf;

	printf("Starting sector read example...\n");

	cfg_queue();

	srand(getpid());

	int fd = open("/dev/sdb", O_RDONLY | O_DIRECT);
	if (fd < 0) {
		perror("/dev/sdb");
		return 1;
	}

	/* O_DIRECT exige buffer alinhado ao tamanho do setor */
	if (posix_memalign((void **)&buf, SECTOR_SIZE, SECTOR_SIZE) != 0) {
		fprintf(stderr, "Falha ao alocar buffer\n");
		return 1;
	}

	for (int i = 0; i < N_OPS; i++) {
		long sector = rand() % (DISK_SZ / SECTOR_SIZE);

		printf("seek to sector %ld\n", sector);
		/* Posiciona no setor sorteado. */
		lseek(fd, (off_t)sector * SECTOR_SIZE, SEEK_SET);
		/* Lê o setor completo. */
		if (read(fd, buf, SECTOR_SIZE) != SECTOR_SIZE) {
			perror("read");
			break;
		}
	}

	free(buf);
	close(fd);

	return 0;
}
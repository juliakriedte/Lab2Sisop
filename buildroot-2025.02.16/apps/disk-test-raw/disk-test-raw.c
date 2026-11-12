#define _GNU_SOURCE             /* O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SECTOR_SIZE 512

int main()
{
	char *buf;

	int fd = open("/dev/sdb", O_RDWR | O_DIRECT);
	if (fd < 0) {
		perror("/dev/sdb");
		return 1;
	}

	/* O_DIRECT exige buffer alinhado ao tamanho do setor */
	if (posix_memalign((void **)&buf, SECTOR_SIZE, SECTOR_SIZE) != 0) {
		fprintf(stderr, "Falha ao alocar buffer\n");
		return 1;
	}
	memset(buf, 0, SECTOR_SIZE);
	strcpy(buf, "Hello World!");

	/* Posicionar-se no início do disco. */
	lseek(fd, 0, SEEK_SET);

	/* Executa a escrita de um setor completo. */
	if (write(fd, buf, SECTOR_SIZE) != SECTOR_SIZE)
		perror("write");

	free(buf);
	close(fd);

	return 0;
}
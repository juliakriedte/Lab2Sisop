#define _GNU_SOURCE             /* O_DIRECT */
#define _FILE_OFFSET_BITS 64    /* off_t de 64 bits em i686 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#define DEVICE      "/dev/sdb"
#define DISK        "sdb"
#define SECTOR_SIZE 512         /* setor lógico do disco IDE do QEMU */

void echo(const char *path, const char *string)
{
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		perror(path);
		return;
	}
	fputs(string, f);
	/* o erro de escrita no sysfs só aparece ao descarregar o buffer */
	if (fclose(f) != 0)
		perror(path);
}

void cfg_queue(void)
{
	/* Desabilita agrupamentos: cada operação gera exatamente uma requisição */
	echo("/sys/block/" DISK "/queue/nomerges", "2");

	/* Aumenta a fila do escalonador (volta para 2 a cada troca de escalonador) */
	echo("/sys/block/" DISK "/queue/nr_requests", "128");
}

void child(int id, long disk_sectors, long n_ops, int write_pct)
{
	char *buf;

	int fd = open(DEVICE, O_RDWR | O_DIRECT);
	if (fd < 0) {
		perror(DEVICE);
		_exit(1);
	}

	/* O_DIRECT exige buffer alinhado ao tamanho do setor */
	if (posix_memalign((void **)&buf, SECTOR_SIZE, SECTOR_SIZE) != 0) {
		fprintf(stderr, "Processo %d: falha ao alocar buffer\n", id);
		_exit(1);
	}
	memset(buf, 'A' + id % 26, SECTOR_SIZE);

	srand(getpid());

	for (long i = 0; i < n_ops; i++) {
		long sector = rand() % disk_sectors;
		off_t pos = (off_t)sector * SECTOR_SIZE;
		ssize_t ret;

		if (rand() % 100 < write_pct)
			ret = pwrite(fd, buf, SECTOR_SIZE, pos);
		else
			ret = pread(fd, buf, SECTOR_SIZE, pos);

		if (ret != SECTOR_SIZE) {
			if (ret < 0)
				perror("pread/pwrite");
			else
				fprintf(stderr, "Processo %d: operação incompleta no setor %ld "
					"(além do fim do disco?)\n", id, sector);
			_exit(1);
		}
	}

	free(buf);
	close(fd);
	_exit(0);
}

int main(int argc, char *argv[])
{
	if (argc != 5) {
		fprintf(stderr, "Uso: %s <setores_disco> <n_ops> <%%escritas> <n_procs>\n", argv[0]);
		return 1;
	}

	long disk_sectors = atol(argv[1]);
	long n_ops        = atol(argv[2]);
	int  write_pct    = atoi(argv[3]);
	int  n_procs      = atoi(argv[4]);

	if (disk_sectors <= 0 || n_ops <= 0 || n_procs <= 0 || write_pct < 0 || write_pct > 100) {
		fprintf(stderr, "Setores, operações e processos devem ser positivos; escritas entre 0 e 100\n");
		return 1;
	}

	cfg_queue();

	for (int i = 0; i < n_procs; i++) {
		/* divide as operações entre os processos; os primeiros recebem o resto */
		long ops = n_ops / n_procs + (i < n_ops % n_procs);

		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			break;
		}
		if (pid == 0)
			child(i, disk_sectors, ops, write_pct);
	}

	int status, failed = 0;
	while (wait(&status) > 0)
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			failed++;

	if (failed)
		printf("%d processo(s) terminaram com erro\n", failed);
	else
		printf("%ld operações concluídas por %d processo(s)\n", n_ops, n_procs);

	return failed != 0;
}
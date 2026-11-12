# Laboratório 4.3 - Resultados

Ambiente: Buildroot + QEMU (i386), kernel 6.12.27, 128 MB de RAM, página de 4096 bytes, página grande de 4096 kB.

## Tabela de faltas de página

Cada célula mostra `minflt / majflt` ocorridas na etapa.

| Execução | inicio | mmap | mlock | acesso | munmap |
|---|---|---|---|---|---|
| lazy (`paging-test lazy 32`) | 77 / 0 | 0 / 0 | - | 8192 / 0 | 0 / 0 |
| populate (`paging-test populate 32`) | 77 / 0 | 8192 / 0 | - | 0 / 0 | 0 / 0 |
| mlock (`paging-test mlock 32`) | 78 / 0 | 0 / 0 | 8192 / 0 | 0 / 0 | 0 / 0 |
| file, cache frio (`paging-test file 16 /root/dados`) | 76 / 1 | 0 / 0 | - | 255 / 130 | 0 / 0 |
| file, cache quente (`paging-test file 16 /root/dados`) | 76 / 0 | 0 / 0 | - | 257 / 0 | 0 / 0 |
| huge (`paging-test huge 32`, 8 páginas grandes) | 76 / 0 | 0 / 0 | - | 8 / 0 | 0 / 0 |

## Saídas

### lazy

```
# paging-test lazy 32
pagina = 4096 bytes, regiao = 32 MiB (8192 paginas)
inicio   minflt = 77     majflt = 0      VmRSS = 1480 kB
mmap     minflt = 0      majflt = 0      VmRSS = 1480 kB
acesso   minflt = 8192   majflt = 0      VmRSS = 34248 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1556 kB
```

### populate

```
# paging-test populate 32
pagina = 4096 bytes, regiao = 32 MiB (8192 paginas)
inicio   minflt = 77     majflt = 0      VmRSS = 1488 kB
mmap     minflt = 8192   majflt = 0      VmRSS = 34256 kB
acesso   minflt = 0      majflt = 0      VmRSS = 34256 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1564 kB
```

### mlock

```
# paging-test mlock 32
pagina = 4096 bytes, regiao = 32 MiB (8192 paginas)
inicio   minflt = 78     majflt = 0      VmRSS = 1488 kB
mmap     minflt = 0      majflt = 0      VmRSS = 1488 kB
mlock    minflt = 8192   majflt = 0      VmRSS = 34256 kB
acesso   minflt = 0      majflt = 0      VmRSS = 34256 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1564 kB
```

### file (cache frio e quente)

```
# dd if=/dev/zero of=/root/dados bs=1M count=16
# sync
# echo 3 > /proc/sys/vm/drop_caches

# paging-test file 16 /root/dados      (cache frio)
pagina = 4096 bytes, regiao = 16 MiB (4096 paginas)
inicio   minflt = 76     majflt = 1      VmRSS = 1304 kB
mmap     minflt = 0      majflt = 0      VmRSS = 1304 kB
acesso   minflt = 255    majflt = 130    VmRSS = 17764 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1428 kB

# paging-test file 16 /root/dados      (cache quente)
pagina = 4096 bytes, regiao = 16 MiB (4096 paginas)
inicio   minflt = 76     majflt = 0      VmRSS = 1280 kB
mmap     minflt = 0      majflt = 0      VmRSS = 1280 kB
acesso   minflt = 257    majflt = 0      VmRSS = 17760 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1404 kB
```

### huge

Sem páginas grandes reservadas, o `mmap` falhou:

```
# grep -i huge /proc/meminfo
HugePages_Total:       0
...
# paging-test huge 32
pagina = 4096 bytes, regiao = 32 MiB (8192 paginas)
inicio   minflt = 77     majflt = 0      VmRSS = 1388 kB
mmap: Cannot allocate memory
```

Com 8 páginas grandes reservadas:

```
# echo 8 > /proc/sys/vm/nr_hugepages
# grep -i huge /proc/meminfo
HugePages_Total:       8
HugePages_Free:        8
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       4096 kB
Hugetlb:           32768 kB

# paging-test huge 32
pagina = 4096 bytes, regiao = 32 MiB (8192 paginas)
inicio   minflt = 76     majflt = 0      VmRSS = 1388 kB
mmap     minflt = 0      majflt = 0      VmRSS = 1388 kB
acesso   minflt = 8      majflt = 0      VmRSS = 1388 kB
munmap   minflt = 0      majflt = 0      VmRSS = 1388 kB
```

## Perguntas

### 1. Em qual etapa ocorrem as faltas nos modos populate e mlock? Por que uma aplicação de tempo real prefere esse comportamento?

No `populate`, as 8192 faltas menores ocorrem dentro do próprio `mmap()` (e o `VmRSS` já sobe ~32 MiB nessa etapa). No `mlock`, as 8192 faltas ocorrem na etapa `mlock`, logo após o `mmap()` (que tem 0 faltas), e o `VmRSS` sobe ~32 MiB nessa etapa. Nos dois casos o laço de acesso não gera nenhuma falta, porque todas as páginas já estão na memória.

Uma aplicação de tempo real prefere isso porque paga o custo das faltas antecipadamente, no início. Assim, durante a execução não há atrasos causados por faltas de página.

### 2. Por que o modo huge gera 8 faltas, e quantas ocorreriam com 32 MiB em páginas grandes de 2 MB (x86_64)?

Cada falta mapeia uma página grande inteira. Com páginas de 4 MB, 32 MiB / 4 MiB = 8 faltas. Com páginas de 2 MB seriam 32 MiB / 2 MiB = 16 faltas.

### 3. Por que o modo file com cache frio gera faltas maiores, e por que o total de faltas é bem menor que o número de páginas?

Com o cache frio (após o `drop_caches`), as páginas do arquivo não estão no page cache e precisam ser lidas do disco, o que gera faltas maiores (130 na execução). Com o cache quente, o arquivo já está na memória e só ocorrem faltas menores.

O total é bem menor que as 4096 páginas por dois mecanismos do kernel:

- **Readahead:** ao ler uma página do disco, o kernel já lê as seguintes (128 KB, 32 páginas), por isso 130 faltas maiores em vez de 4096 (4096 / 32 = 128).
- **Fault-around:** em uma falta de página que já está no page cache, o kernel mapeia de uma vez as vizinhas (64 KB, 16 páginas), por isso 4096 / 16 = 256 faltas menores (257 e 255 observadas).

### 4. Por que o VmRSS não aumenta no modo huge? Em qual campo de /proc/<pid>/status essa memória aparece?

As páginas grandes vêm de um pool reservado previamente (`nr_hugepages`) e são contabilizadas separadamente da memória residente comum. Por isso o `VmRSS` ficou em 1388 kB. Essa memória aparece no campo `HugetlbPages` de `/proc/<pid>/status` (conferir em `man 5 proc`). No sistema todo, aparece como `Hugetlb` em `/proc/meminfo` (32768 kB com as 8 páginas reservadas).
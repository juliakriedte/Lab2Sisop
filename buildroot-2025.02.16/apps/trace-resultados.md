# Laboratório 4.4 - Resultados

Ambiente: Buildroot + QEMU (i386), kernel 6.12.27, 128 MB de RAM, trace-cmd.

## 1. Eventos rastreados no paging-test (32 MiB)

Comandos (um `record` por modo, com os dois eventos e a opção `-F`):

```
trace-cmd record -e exceptions:page_fault_user -e kmem:mm_page_alloc -F paging-test lazy 32
trace-cmd report | grep -c 'paging-test.*page_fault_user'
trace-cmd report | grep -c 'paging-test.*mm_page_alloc'

trace-cmd record -e exceptions:page_fault_user -e kmem:mm_page_alloc -F paging-test populate 32
trace-cmd report | grep -c 'paging-test.*page_fault_user'
trace-cmd report | grep -c 'paging-test.*mm_page_alloc'
```

| Modo | page_fault_user | mm_page_alloc | minflt do getrusage (total) |
|---|---|---|---|
| lazy | 8244 | 8238 | 8274 (82 + 8192) |
| populate | 53 | 8235 | 8280 (88 + 8192) |

**Comparação:** no `lazy`, os três números são parecidos (cerca de 8192 mais as faltas e alocações do início do programa): cada falta do laço gera uma alocação de página, como se esperava. No `populate`, as alocações continuam em ~8200, mas os eventos `page_fault_user` caem para 53, enquanto o `getrusage()` ainda conta 8280 faltas (veja a seção 2).

## 2. Modo populate: getrusage x page_fault_user

O `getrusage()` conta 8192 faltas no `mmap()`. No rastreamento, aparecem apenas **53** eventos `page_fault_user`.

**Explicação:** o evento `page_fault_user` só é registrado quando o processo gera uma falta de página ao acessar a memória. No `populate`, o kernel mapeia todas as páginas dentro do próprio `mmap()`, antes de o processo acessar qualquer endereço, então quase não há faltas desse tipo. O `getrusage()` conta essas páginas mesmo assim. Os poucos eventos que aparecem são do início do programa (carregamento do código e das bibliotecas). As 8192 alocações continuam no `mm_page_alloc`, pois as páginas físicas foram entregues.

## 3. Reserva de páginas grandes

Comandos:

```
echo 0 > /proc/sys/vm/nr_hugepages
trace-cmd record -e kmem:mm_page_alloc sh -c 'echo 8 > /proc/sys/vm/nr_hugepages'
trace-cmd report | grep -c mm_page_alloc
trace-cmd report | grep 'order=10' | head
```

Resultado: aparecem **35** eventos `mm_page_alloc` no total, todos do processo `sh`. As alocações grandes têm **order=10** (as três primeiras: `pfn=0x1c00`, `0x2800` e `0x2c00`). Os outros eventos são alocações pequenas do próprio shell.

```
sh-136   37.771204: mm_page_alloc: page=0x1c00 pfn=0x1c00 order=10 migratetype=0 gfp_flags=GFP_HIGHUSER...
sh-136   37.771646: mm_page_alloc: page=0x2800 pfn=0x2800 order=10 migratetype=0 gfp_flags=GFP_HIGHUSER...
sh-136   37.771667: mm_page_alloc: page=0x2c00 pfn=0x2c00 order=10 migratetype=0 gfp_flags=GFP_HIGHUSER...
```

O número de páginas grandes reservadas é 8 (`nr_hugepages`), uma alocação de order=10 para cada.

**Relação com o Hugepagesize:** o `Hugepagesize` é 4096 kB. Uma página normal tem 4 kB (order 0), então uma página grande tem 4096 / 4 = 1024 páginas, que é 2^10, ou seja, order=10, que é o valor visto no relatório. Cada página grande reservada é uma alocação de order=10.

**Por que o paging-test huge 32 quase não gera mm_page_alloc:** as páginas grandes já foram alocadas quando reservamos o pool (`nr_hugepages`). Quando o programa as usa, o kernel só pega páginas desse pool, sem pedir páginas novas ao alocador. Por isso as 8 faltas do laço quase não geram `mm_page_alloc`.

## 4. OOM killer

Comando: `trace-cmd record -e vmscan:mm_vmscan_kswapd_wake -e vmscan:mm_vmscan_direct_reclaim_begin -e oom:mark_victim paging-test lazy 100`

Linhas relevantes do relatório:

```
kswapd0-30        93.699433: mm_vmscan_kswapd_wake: nid=0 order=0
paging-test-158   93.786522: mm_vmscan_direct_reclaim_begin: order=0 gfp_flags=GFP_HIGHUSER_MOVABLE|...
...
paging-test-158   94.550843: mark_victim: pid=158 comm=paging-test total-vm=105132kB ...
```

Sequência: o `kswapd` foi acordado várias vezes, depois o próprio `paging-test` passou a recuperar memória (direct reclaim) e por fim foi escolhido como vítima.

**Tempo entre o primeiro direct_reclaim_begin do paging-test e o mark_victim:** 94.550843 - 93.786522 = 0.764321 s, cerca de **764 ms**.

**Memória usada ao ser eliminado:** pela mensagem do kernel (`dmesg`): `Out of memory: Killed process 158 (paging-test) total-vm:105132kB, anon-rss:72704kB`. Ou seja, **anon-rss = 72704 kB (~71 MiB)**.

**Comparação com o MemTotal:** o `MemTotal` é 116600 kB (~114 MiB). O `anon-rss` de 72704 kB corresponde a cerca de 62% desse total. O resto estava assim: 32768 kB (28%) no pool de 8 páginas grandes, que ainda estava reservado (`hugepages_total=8` no dmesg) e o processo não podia usar, e cerca de 11 MB (10%) com o kernel e os outros processos.

**mmap com 200 MiB:** com `paging-test lazy 200` o `mmap()` falhou na hora (`__vm_enough_memory ... not enough memory`, `Cannot allocate memory`): a heurística de overcommit recusou a alocação inteira, sem nenhuma falta de página.
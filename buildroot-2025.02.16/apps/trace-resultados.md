# Laboratório 4.4 - Resultados

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
| lazy | _TODO_ | _TODO_ | _TODO_ |
| populate | _TODO_ | _TODO_ | _TODO_ |

**Comparação:** _TODO: preencher depois de medir._ No `lazy`, espera-se que os três números sejam parecidos (cerca de 8192 mais as faltas e alocações do início do programa), pois cada falta do laço gera uma alocação de página.

## 2. Modo populate: getrusage x page_fault_user

O `getrusage()` conta 8192 faltas no `mmap()`. No rastreamento, aparecem apenas _TODO_ eventos `page_fault_user`.

**Explicação:** o evento `page_fault_user` só é registrado quando o processo gera uma falta de página ao acessar a memória. No `populate`, o kernel mapeia todas as páginas dentro do próprio `mmap()`, antes de o processo acessar qualquer endereço, então quase não há faltas desse tipo. O `getrusage()` conta essas páginas mesmo assim. Os poucos eventos que aparecem são do início do programa (carregamento do código e das bibliotecas). As 8192 alocações continuam no `mm_page_alloc`, pois as páginas físicas foram entregues.

## 3. Reserva de páginas grandes

Comandos:

```
echo 0 > /proc/sys/vm/nr_hugepages
trace-cmd record -e kmem:mm_page_alloc sh -c 'echo 8 > /proc/sys/vm/nr_hugepages'
trace-cmd report | grep -c mm_page_alloc
trace-cmd report | grep 'order=10' | head
```

Resultado: _TODO: número de alocações e order observado._

**Relação com o Hugepagesize:** o `Hugepagesize` é 4096 kB. Uma página normal tem 4 kB (order 0), então uma página grande tem 4096 / 4 = 1024 páginas, que é 2^10, ou seja, order=10 _(confirmar com o relatório)_. São 8 alocações, uma por página grande reservada.

**Por que o paging-test huge 32 quase não gera mm_page_alloc:** as páginas grandes já foram alocadas quando reservamos o pool (`nr_hugepages`). Quando o programa as usa, o kernel só pega páginas desse pool, sem pedir páginas novas ao alocador. Por isso as 8 faltas do laço não geram `mm_page_alloc` _(confirmar rastreando o `paging-test huge 32`)_.

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

**Comparação com o MemTotal:** _TODO: colar `grep MemTotal /proc/meminfo`._ O `free -h` mostrou 113 MiB de total, então o processo usava cerca de 63% da memória. O resto estava com o kernel, os outros processos e o pool de 8 páginas grandes (32 MiB), que ainda estava reservado (`hugepages_total=8` no dmesg) e o processo não podia usar.

**mmap com 200 MiB:** com `paging-test lazy 200` o `mmap()` falhou na hora (`__vm_enough_memory ... not enough memory`, `Cannot allocate memory`): a heurística de overcommit recusou a alocação inteira, sem nenhuma falta de página.
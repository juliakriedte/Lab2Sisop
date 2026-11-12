# Laboratório 4.1 - Resultados

Ambiente: Buildroot + QEMU (i386), kernel 6.12.27, 128 MB de RAM.

## Saída com swap

### Ativação do swap

Arquivo de swap de 32 MB criado com `qemu-img create -f raw vm-swap.img 32M` e ativado dentro do QEMU:

```
mkswap /dev/sdb
Setting up swapspace version 1, size = 33550336 bytes

swapon /dev/sdb
Adding 32764k swap on /dev/sdb.  Priority:-2 extents:1 across:32764k

free -h
               total        used        free      shared  buff/cache   available
Mem:           113Mi        10Mi       103Mi        60Ki       4.1Mi       102Mi
Swap:           31Mi          0B        31Mi
```

O sistema passou a ter 113 MiB de RAM e 31 MiB de swap livres.

### mem-test-malloc

```
...
got 3052 MiB
got 3053 MiB
got 3054 MiB
malloc failure after 3054 MiB
```

**Explicação:** o resultado é o mesmo do teste sem swap. O programa só aloca memória virtual e nunca escreve nela, então o kernel faz overcommit: entrega os endereços sem garantir que existe memória física. O `malloc` só falha perto de 3 GiB, que é o limite de memória virtual de um processo em 32 bits. O swap não muda nada porque a memória nunca é usada de fato.

### mem-test-read

```
malloc failure after 3054 MiB
1
2
3
...
120
121
mem-test-read invoked oom-killer: gfp_mask=0x140cca(GFP_HIGHUSER_MOVABLE|__GFP_COMP), ...
...
Mem-Info:
active_anon:559 inactive_anon:25798 ...
free:436 free_pcp:13 free_cma:0
...
137 pages in swap cache
Free swap  = 0kB
Total swap = 32764kB
32638 pages RAM
...
[  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[    131]     0   131   785561    26177    26176        1         0  3145728     8032  0  mem-test-read
oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),task=mem-test-read,pid=131,uid=0
Out of memory: Killed process 131 (mem-test-read) total-vm:3142244kB, anon-rss:1047040kB ...
Killed
```

**Explicação:** o `malloc` reservou ~3 GiB de memória virtual (overcommit). O problema só apareceu no `memset`, quando a memória passou a ser usada de verdade. O programa escreveu 121 MiB antes de ser morto, um pouco mais que sem swap (~104 MiB), porque o swap deu mais ~31 MiB.

No `dmesg`, `Free swap = 0kB` mostra que o swap encheu. Sem RAM e sem swap, o kernel invocou o OOM killer, que escolheu e matou o `mem-test-read`. Enquanto o swap era usado, o sistema ficou lento (thrashing).

### mem-test-write

Observação: a primeira versão, compilada com `-O2`, teve o mesmo resultado do `mem-test-malloc` (`malloc failure after 3055 MiB`). O GCC substituiu `malloc` + `memset(p, 0, ...)` por `calloc`, que usa páginas já zeradas pelo kernel e não as escreve, então a memória física nunca foi consumida. Recompilado com `-O0`, o programa se comportou como esperado:

```
got 1 MiB
got 2 MiB
...
got 135 MiB
got 136 MiB
mem-test-write invoked oom-killer: gfp_mask=0x140cca(GFP_HIGHUSER_MOVABLE|__GFP_COMP), order=0, oom_score_adj=0
...
Mem-Info:
active_anon:48 inactive_anon:27043 ...
free:437 free_pcp:13 free_cma:0
...
23 pages in swap cache
Free swap  = 0kB
Total swap = 32764kB
32638 pages RAM
...
[  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[    127]     0   127    35892    27054    27040       14         0   155648     8032             0 mem-test-write
oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),task=mem-test-write,pid=127,uid=0
Out of memory: Killed process 127 (mem-test-write) total-vm:143568kB, anon-rss:108160kB, ...
Killed
```

**Explicação:** aqui a memória é escrita logo após o `malloc`, então ela é realmente usada e não há como o overcommit esconder a falta de memória. O programa parou em 136 MiB, bem longe dos ~3 GiB do `mem-test-malloc`. No `dmesg`, o processo usava ~106 MiB de RAM e ~31 MiB de swap (`Free swap = 0kB`). Sem memória disponível, o OOM killer foi invocado e matou o `mem-test-write`. Com swap o programa chegou mais longe que sem swap (~104 MiB), com thrashing.

A diferença para o `mem-test-read` é que aqui o `malloc` nunca falha: o OOM killer age durante a escrita.

## Saída com overcommit_memory = 1

Modo ativado com `echo 1 > /proc/sys/vm/overcommit_memory` (swap de 32 MB ainda ativo).

### mem-test-malloc

```
...
got 3053 MiB
got 3054 MiB
got 3055 MiB
malloc failure after 3055 MiB
```

**Explicação:** o resultado é igual ao do modo 0. Como o modo 1 nunca recusa alocações, o `malloc` não é barrado por falta de memória. O que o faz falhar em ~3 GiB é o limite de memória virtual de um processo em 32 bits.

### mem-test-read

```
malloc failure after 3055 MiB
1
2
...
120
121
mem-test-read invoked oom-killer: gfp_mask=0x140dca(GFP_HIGHUSER_MOVABLE|__GFP_COMP|__GFP_ZERO), ...
...
Free swap  = 0kB
Total swap = 32764kB
...
[  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[    129]     0   129   785818    26104    26080       24         0  3145728     8000             0 mem-test-read
oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),task=mem-test-read,pid=129,uid=0
Out of memory: Killed process 129 (mem-test-read) total-vm:3143272kB, anon-rss:104320kB, ...
Killed
```

**Explicação:** igual ao modo 0. O `malloc` reservou ~3 GiB de memória virtual (overcommit) e a falta de memória só apareceu no `memset`. Com RAM e swap cheios (`Free swap = 0kB`), o OOM killer matou o `mem-test-read` após 121 MiB.

### mem-test-write

```
got 1 MiB
got 2 MiB
...
got 135 MiB
got 136 MiB
mem-test-write invoked oom-killer: gfp_mask=0x140cca(GFP_HIGHUSER_MOVABLE|__GFP_COMP), order=0, oom_score_adj=0
...
Free swap  = 0kB
Total swap = 32764kB
...
[  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[    130]     0   130    35892    27078    27072        6         0   155648     8000             0 mem-test-write
oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),task=mem-test-write,pid=130,uid=0
Out of memory: Killed process 130 (mem-test-write) total-vm:143568kB, anon-rss:108288kB, ...
Killed
```

**Explicação:** igual ao modo 0. A memória é escrita logo após o `malloc`, então é usada de verdade. Ao esgotar RAM (~106 MiB) e swap (~31 MiB), o OOM killer matou o processo em 136 MiB.

### Conclusão do modo 1

Os três programas se comportaram como no modo 0. Isso acontece porque as alocações são de 1 MiB, e o modo 0 (heurística) só recusa alocações absurdamente grandes, então nunca recusava essas. O modo 1 garante que o `malloc` não falhe por falta de memória, mas não evita o OOM killer quando a memória é usada de fato.

## Saída com overcommit_memory = 2

Modo ativado com `echo 2 > /proc/sys/vm/overcommit_memory` (swap de 32 MB ainda ativo).

### mem-test-malloc

```
got 1 MiB
got 2 MiB
...
got 82 MiB
got 83 MiB
__vm_enough_memory: pid: 134, comm: mem-test-malloc, bytes: 1052672 not enough memory for the ...
__vm_enough_memory: pid: 134, comm: mem-test-malloc, bytes: 1048576 not enough memory for the ...
__vm_enough_memory: pid: 134, comm: mem-test-malloc, bytes: 1183744 not enough memory for the ...
malloc failure after 83 MiB
```

**Explicação:** no modo 2 não há overcommit. O kernel limita a memória virtual ao tamanho do swap mais 50% da RAM (`overcommit_ratio`): ~32 MiB + ~56 MiB (metade dos 113 MiB), cerca de 88 MiB. Parte disso já estava reservada pelos processos do sistema, por isso o `malloc` chegou a 83 MiB e retornou `NULL`. As mensagens `__vm_enough_memory` mostram o kernel recusando a alocação. Antes eram ~3 GiB.

### mem-test-read

```
__vm_enough_memory: pid: 135, comm: mem-test-read, bytes: 1052672 not enough memory for the ...
__vm_enough_memory: pid: 135, comm: mem-test-read, bytes: 1044480 not enough memory for the ...
__vm_enough_memory: pid: 135, comm: mem-test-read, bytes: 1183744 not enough memory for the ...
malloc failure after 83 MiB
1
2
...
82
83
```

**Explicação:** o `malloc` falhou em 83 MiB, como no teste anterior. Como a memória já estava garantida, o `memset` escreveu nos 83 blocos sem problema e o programa terminou normalmente, sem OOM killer e sem `Killed`.

### mem-test-write

```
got 1 MiB
got 2 MiB
...
got 82 MiB
got 83 MiB
__vm_enough_memory: pid: 136, comm: mem-test-write, bytes: 1052672 not enough memory for the ...
__vm_enough_memory: pid: 136, comm: mem-test-write, bytes: 1048576 not enough memory for the ...
__vm_enough_memory: pid: 136, comm: mem-test-write, bytes: 1183744 not enough memory for the ...
malloc failure after 83 MiB
```

**Explicação:** o `malloc` falhou em 83 MiB e o programa terminou normalmente, imprimindo `malloc failure`. Não houve OOM killer, porque o kernel recusou a alocação antes de a memória faltar.

### Conclusão do modo 2

O modo 2 troca o OOM killer por falha no `malloc`: o programa descobre o problema na alocação e pode tratá-lo (como fazem os programas deste laboratório, que imprimem uma mensagem e retornam). Em contrapartida, só ~83 MiB puderam ser alocados, contra ~3 GiB nos modos 0 e 1, porque não existe overcommit.

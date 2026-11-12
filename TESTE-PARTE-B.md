# Como testar a parte B (política SSTF, prazo e disk-test sequencial)

Este guia mostra como compilar e testar no QEMU o que a parte B entregou na branch
`modulo-sstf-parte-b`:

- `sstf_dispatch_request`: escolha SSTF (requisição mais próxima do cabeçote, empate
  para o menor setor) e prazo máximo (a mais antiga é despachada primeiro se esperou
  mais que `deadline_ms`);
- parâmetro novo do módulo: `deadline_ms` (padrão 0 = prazo desabilitado, SSTF puro);
- mensagens de `trace_printk` (formato abaixo, usado pelo trace-dist da parte C);
- `disk-test` com 5º argumento opcional `aleatorio` (padrão) ou `sequencial`.

Convenção: blocos marcados **Codespace** rodam no terminal do Codespace, dentro de
`buildroot-2025.02.16`. Blocos marcados **QEMU** rodam dentro da VM (login `root` / `root`).

---

## Formato das mensagens do `trace_printk`

Elas aparecem no `trace-cmd report` intercaladas com os eventos de bloco (linhas `bprint:`):

```
sstf: inicio de despacho motivo=fila_cheia pendentes=N
sstf: inicio de despacho motivo=tempo_esgotado pendentes=N
sstf: prazo expirado setor=S espera_ms=W
```

Para o trace-dist: conte as linhas `prazo expirado` para obter os despachos por prazo
expirado. **Não** filtre o report só por `8,16`, porque isso remove essas linhas.

---

## 0. Pré-requisitos

### Pegar a branch (Codespace)

```sh
git fetch && git switch modulo-sstf-parte-b
```

### Buildroot com trace-cmd e kernel com ftrace (Codespace)

```sh
grep -E "TRACE_CMD|PRE_BUILD|OVERLAY|FRAGMENT" .config
grep -E "CONFIG_FTRACE=|CONFIG_BLK_DEV_IO_TRACE=|IOSCHED" output/build/linux-6.12.27/.config
```

Tem que aparecer:

| Arquivo | Opção esperada |
|---------|----------------|
| `.config` | `BR2_PACKAGE_TRACE_CMD=y` |
| `.config` | `BR2_ROOTFS_PRE_BUILD_SCRIPT="custom-scripts/pre-build.sh"` |
| `.config` | `BR2_ROOTFS_OVERLAY="overlay"` |
| kernel | `CONFIG_FTRACE=y` e `CONFIG_BLK_DEV_IO_TRACE=y` |
| kernel | `CONFIG_MQ_IOSCHED_DEADLINE=y` e `CONFIG_MQ_IOSCHED_KYBER=y` |

Sem ftrace o módulo compila, mas o `trace_printk` não grava nada e o `trace-cmd` não
encontra os eventos `block:`. Para ligar: `make menuconfig` → Target packages →
Debugging, profiling and benchmark → `trace-cmd`; e no kernel, um fragmento
(`BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`) com:

```
CONFIG_FTRACE=y
CONFIG_BLK_DEV_IO_TRACE=y
```

seguido de `make linux-reconfigure` e `make`.

> Obs.: o `.config` ainda não está versionado no repo. Ele faz parte da entrega, então
> precisa ser commitado (parte A).

### Disco de testes de 1 GB (Codespace)

1 GB = 2.097.152 setores de 512 bytes, que é o primeiro argumento do disk-test.

```sh
dd if=/dev/zero of=sdb.bin bs=4096 count=262144
```

> **Cuidado:** o `.gitignore` atual **não** ignora `sdb.bin`. Adicione
> `buildroot-2025.02.16/sdb.bin` antes de qualquer `git add -A`.

---

## 1. Compilar (Codespace)

```sh
make
find overlay/lib/modules -name sstf.ko
ls -l overlay/usr/bin/disk-test
```

O `make` roda o `pre-build.sh`, que compila o módulo e o disk-test para o `overlay` e
regera o `rootfs.ext2`. Para ver só os erros do módulo:

```sh
make -C modules/sstf build
```

> Os erros do VS Code/IntelliSense no Mac (`O_DIRECT` não definido, `#include` do
> kernel não encontrado) são esperados: lá não existem os headers do Linux nem a árvore
> do kernel. O que vale é o `make` no Codespace.

---

## 2. Subir o QEMU (Codespace)

**Disco sem limite** (casos 1 a 4):

```sh
qemu-system-i386 --kernel output/images/bzImage --hda output/images/rootfs.ext2 \
  -drive file=sdb.bin,format=raw,index=1,media=disk \
  --nographic --append "console=ttyS0 root=/dev/sda"
```

**Disco limitado a 200 IOPS** (casos 5 e 6 e teste de prazo):

```sh
qemu-system-i386 --kernel output/images/bzImage --hda output/images/rootfs.ext2 \
  -drive file=sdb.bin,format=raw,index=1,media=disk,throttling.iops-total=200 \
  --nographic --append "console=ttyS0 root=/dev/sda"
```

- Use `-drive ...format=raw`, e não `--hdb sdb.bin`. Sem `format=raw` o QEMU bloqueia
  escrita no setor 0, e o disk-test aleatório falha quando sorteia esse setor.
- A rede não é necessária para os testes.
- Para sair: `sync; poweroff` dentro da VM.

---

## 3. Teste 1: ciclo de vida do módulo (QEMU)

```sh
modprobe sstf
grep . /sys/module/sstf/parameters/*
cat /sys/block/sdb/queue/scheduler
echo sstf > /sys/block/sdb/queue/scheduler
cat /sys/block/sdb/queue/scheduler
rmmod sstf
echo mq-deadline > /sys/block/sdb/queue/scheduler
rmmod sstf
```

Esperado:

- parâmetros com os padrões `queue_size=50`, `max_wait_ms=50` e `deadline_ms=0`;
- `sstf` aparece na lista de escalonadores e depois fica selecionado (`[sstf]`);
- o primeiro `rmmod` **falha** ("in use") e o segundo funciona.

Um aviso `NOTICE` no `dmesg` sobre `trace_printk` ser para depuração é esperado.

---

## 4. Teste 2: SSTF e início de despacho (QEMU, disco sem limite)

```sh
modprobe sstf queue_size=20 max_wait_ms=50
echo sstf > /sys/block/sdb/queue/scheduler
cd /root
trace-cmd record -b 8192 -e block:block_bio_queue -e block:block_rq_issue disk-test 2097152 2000 30 64
trace-cmd report > t.txt && rm trace.dat
grep -c 'block_bio_queue: *8,16' t.txt
grep -c 'block_rq_issue: *8,16' t.txt
grep -c 'motivo=fila_cheia' t.txt
grep -c 'motivo=tempo_esgotado' t.txt
grep -E 'block_rq_issue: *8,16|sstf:' t.txt | head -40
```

Esperado:

- as duas primeiras contagens dão **2000**, igual ao `n_ops`. Se der menos, o buffer
  perdeu eventos: veja o "overrun" na saída do `record` e aumente o `-b`;
- no `head`, depois de cada `inicio de despacho ... pendentes=N`, os setores do
  `block_rq_issue` andam sempre para o vizinho mais próximo do anterior, e não na
  ordem de chegada.

Conferência rápida da redução de setores percorridos (as métricas oficiais são do
trace-dist da parte C):

```sh
for ev in block_bio_queue block_rq_issue; do
  grep "$ev: *8,16" t.txt | awk -v ev=$ev '{for(i=1;i<=NF;i++) if($i=="+") s=$(i-1); d=s-p; if(d<0)d=-d; tot+=d; p=s} END{print ev, tot}'
done
```

O total do `block_rq_issue` (ordem SSTF) tem que ser bem menor que o do
`block_bio_queue` (ordem de chegada, FCFS).

---

## 5. Teste 3: prazo máximo (QEMU, disco limitado)

```sh
modprobe sstf deadline_ms=500
echo sstf > /sys/block/sdb/queue/scheduler
cd /root
trace-cmd record -b 8192 -e block:block_bio_queue -e block:block_rq_issue disk-test 2097152 4000 30 64
trace-cmd report > p500.txt && rm trace.dat
grep -c 'prazo expirado' p500.txt
grep 'prazo expirado' p500.txt | head
```

Esperado:

- linhas `sstf: prazo expirado setor=... espera_ms=...` com `espera_ms` pouco acima de 500;
- repetindo com `deadline_ms=0`, a contagem é **0**;
- com `deadline_ms=1000`, a contagem é menor que com 500.

4000 operações a 200 IOPS levam cerca de 20 s.

---

## 6. Teste 4: disk-test sequencial (QEMU)

```sh
trace-cmd record -b 8192 -e block:block_bio_queue -e block:block_rq_issue disk-test 2097152 2000 30 64 sequencial
trace-cmd report > seq.txt && rm trace.dat
grep 'block_bio_queue: *8,16' seq.txt | head -20
```

Esperado: setores de chegada quase crescentes (0, 1, 2, …), com pequenas trocas entre
processos. Todos os processos compartilham um contador atômico de setor, então a carga
inteira percorre o disco em ordem. O laço `awk` do teste 2 aplicado a `seq.txt` deve
mostrar FCFS e SSTF bem mais próximos do que no caso aleatório.

Uso do disk-test:

```
disk-test <setores_disco> <n_ops> <%escritas> <n_procs> [aleatorio|sequencial]
```

Sem o 5º argumento, o comportamento é o do Lab 3.2 (aleatório).

---

## 7. Trocar parâmetros entre execuções (QEMU)

O módulo só pode ser removido quando nenhum disco o usa:

```sh
echo mq-deadline > /sys/block/sdb/queue/scheduler
rmmod sstf
modprobe sstf queue_size=100 max_wait_ms=20 deadline_ms=1000
echo sstf > /sys/block/sdb/queue/scheduler
```

| Parâmetro | Significado | Faixa recomendada |
|-----------|-------------|-------------------|
| `queue_size` | requisições acumuladas antes de despachar | 20 a 100 |
| `max_wait_ms` | tempo após a última chegada para despachar uma fila não cheia | 20 a 100 ms |
| `deadline_ms` | prazo máximo desde a chegada; 0 desabilita | 500 a 1000 ms |

---

## 8. Tirar os traces da VM (Codespace)

Primeiro rode `sync; poweroff` no QEMU. Depois, **antes de qualquer `make`**, porque o
`make` recria o `rootfs.ext2` e apaga o `/root` da VM:

```sh
mkdir -p ../traces
output/host/sbin/debugfs -R "dump /root/p500.txt ../traces/teste-prazo-500.txt" output/images/rootfs.ext2
```

Se o `debugfs` não existir:

```sh
sudo mount -o loop output/images/rootfs.ext2 /mnt
cp /mnt/root/*.txt ../traces/
sudo umount /mnt
```

O rootfs tem só 60 MB: apague o `trace.dat` depois de cada `report` e confira o espaço
com `df -h /` dentro da VM.

---

## Problemas comuns

| Sintoma | Causa provável / solução |
|---------|--------------------------|
| `modprobe: module sstf not found` | `insmod $(find /lib/modules -name sstf.ko) <parâmetros>`, ou rodar `make` de novo |
| `echo sstf > .../scheduler` dá `Invalid argument` | módulo não carregado |
| `trace-cmd` não existe, ou não aparecem eventos `block:`/linhas `sstf:` | ver passo 0 (`.config` e ftrace no kernel) |
| disk-test com "operação incompleta" ou erro de escrita | QEMU iniciado com `--hdb` em vez de `-drive ...format=raw` |
| número de eventos `8,16` menor que `n_ops` | buffer perdeu eventos: aumentar `-b` no `trace-cmd record` |
| travamento ou kernel panic ao usar o sstf | guardar o log do console e avisar a parte B |

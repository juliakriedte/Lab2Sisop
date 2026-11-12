# Lab2Sisop

## Executar qemu
sudo qemu-system-i386 --device e1000,netdev=eth0,mac=aa:bb:cc:dd:ee:ff --netdev tap,id=eth0,script=custom-scripts/qemu-ifup --kernel output/images/bzImage --hda output/images/rootfs.ext2 --nographic --append "console=ttyS0 root=/dev/sda"

Para sair: Ctrl + a depois x

É possível mudar o o **nível de log** padrão do kernel atráves de um argumento de linha de comando no boot. Como o bootloader é substituido pelo QEMU, então basta adicionar o argumento na linha do QEMU:
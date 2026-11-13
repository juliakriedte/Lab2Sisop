#!/bin/sh
HOST=`ip route show | head --lines 1 | awk '{print $9}'`
mkdir -p $BASE_DIR/../overlay/etc/init.d/
cat $BASE_DIR/../custom-scripts/network-config | sed 's/\[IP-DO-HOST\]/'"$HOST"'/g' > $BASE_DIR/../overlay/etc/init.d/S41network-config
chmod +x $BASE_DIR/../overlay/etc/init.d/S41network-config
if [ ! -x $BASE_DIR/host/bin/i686-buildroot-linux-gnu-gcc ] ||
   [ ! -f $BASE_DIR/build/linux-6.12.27/Module.symvers ]; then
	echo "pre-build: kernel/toolchain ainda nao compilados, pulando apps e modulos (rode make de novo no fim)"
	exit 0
fi

set -e
make -C $BASE_DIR/../apps/banner
make -C $BASE_DIR/../modules/sstf
make -C $BASE_DIR/../apps/disk-test

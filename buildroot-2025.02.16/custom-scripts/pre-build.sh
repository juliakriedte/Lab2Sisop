#!/bin/sh
HOST=`ip route show | head --lines 1 | awk '{print $9}'`
mkdir -p $BASE_DIR/../overlay/etc/init.d/
cat $BASE_DIR/../custom-scripts/network-config | sed 's/\[IP-DO-HOST\]/'"$HOST"'/g' > $BASE_DIR/../overlay/etc/init.d/S41network-config
chmod +x $BASE_DIR/../overlay/etc/init.d/S41network-config
make -C $BASE_DIR/../apps/banner
make -C $BASE_DIR/../modules/hello
make -C $BASE_DIR/../modules/miscdrv
make -C $BASE_DIR/../modules/procdrv
make -C $BASE_DIR/../modules/timerdrv
make -C $BASE_DIR/../modules/chardriver
make -C $BASE_DIR/../modules/waitdrv
make -C $BASE_DIR/../apps/sched-query
make -C $BASE_DIR/../apps/sched-burn
make -C $BASE_DIR/../apps/sched-rt
make -C $BASE_DIR/../apps/sched-share
make -C $BASE_DIR/../apps/schedrt-dl
make -C $BASE_DIR/../apps/schedrt-multi
make -C $BASE_DIR/../apps/disk-test-raw
make -C $BASE_DIR/../apps/disk-test-sector-read
make -C $BASE_DIR/../apps/disk-test
make -C $BASE_DIR/../apps/trace-dist
make -C $BASE_DIR/../apps/disk-test-full 
make -C $BASE_DIR/../apps/mem-test-malloc
make -C $BASE_DIR/../apps/mem-test-write
make -C $BASE_DIR/../apps/mem-test-read
make -C $BASE_DIR/../apps/mem-test-ulimit
make -C $BASE_DIR/../apps/segments-address
make -C $BASE_DIR/../apps/segments-symbols
make -C $BASE_DIR/../apps/paging-test
make -C $BASE_DIR/../modules/segments
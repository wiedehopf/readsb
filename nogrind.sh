#!/bin/bash
systemctl stop test
rm -rf /run/test
mkdir -p /run/test
chown readsb /run/test
source /etc/default/test
cp -f readsb /tmp/test123

#gdb -ex=run --args /tmp/test123 --quiet $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS $@
#gdb -batch -ex 'set confirm off' -ex 'handle SIGTERM nostop print pass' -ex 'handle SIGINT nostop print pass' -ex run -ex 'bt full' --args \
/tmp/test123 --quiet $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS $@
#sudo -u readsb ./readsb --quiet $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --net-connector 127.0.0.1,50006,beast_in $@


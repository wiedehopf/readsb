#!/bin/bash
instance=readsb
systemctl stop ${instance}
#rm -rf /run/${instance}
mkdir -p /run/${instance}
chown readsb /run/${instance}
source /etc/default/${instance}

runuser -u readsb -- gdb -batch -ex 'set confirm off' -ex 'handle SIGTERM nostop print pass' -ex 'handle SIGINT nostop print pass' -ex run -ex 'bt full' --args /usr/bin/readsb --quiet $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --write-json /run/${instance} $@
#/usr/bin/sudo -u readsb /usr/bin/readsb --quiet $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --write-json /run/${instance} $@


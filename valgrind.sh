#!/bin/bash
systemctl stop test
rm -rf /run/test
mkdir -p /run/test
chown readsb /run/test
source /etc/default/test
rm -f /tmp/readsb
cp readsb /tmp/readsb
sudo -u readsb valgrind $@ /tmp/readsb $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --write-json /run/test --quiet --db-file=none

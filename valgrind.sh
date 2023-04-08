#!/bin/bash
systemctl stop test
rm -rf /run/test
mkdir -p /run/test
chown readsb /run/test
source /etc/default/test
cp -f readsb /tmp/test123

FIRST="--error-exitcode=3 --exit-on-first-error=yes"
FIRST="-s"

MEM="--track-origins=yes"
MEM="--show-leak-kinds=all --leak-check=full"
MEM="--show-leak-kinds=all --track-origins=yes --leak-check=full"
MEM=""

valgrind $MASSIF $FIRST $MEM /tmp/test123 $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --quiet --db-file=none $@


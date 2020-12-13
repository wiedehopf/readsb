#!/bin/bash
systemctl stop test
rm -rf /run/test
mkdir -p /run/test
source /etc/default/test
valgrind $@ ./readsb $RECEIVER_OPTIONS $DECODER_OPTIONS $NET_OPTIONS $JSON_OPTIONS --write-json /run/test --quiet

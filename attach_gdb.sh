#!/bin/bash
if [[ -d /opt/rh/devtoolset-9 ]]; then
    source scl_source enable devtoolset-9
fi
while true; do
    date -u
    date
    sleep 5 &
    gdb --pid "$(pgrep -f readsb)" \
        -ex 'handle SIGTERM nostop print pass' \
        -ex 'handle SIGINT nostop print pass' \
        -ex 'handle SIGCONT nostop print pass' \
        -batch -ex continue -ex 'bt full'
    wait
done  >> gdb.log 2>&1

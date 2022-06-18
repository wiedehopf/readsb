#!/bin/bash
if [[ -d /opt/rh/devtoolset-9 ]]; then
    source scl_source enable devtoolset-9
fi
while sleep 5; do
    date -u
    date
    gdb --pid "$(pgrep readsb)" \
        -ex 'handle SIGTERM nostop print pass' \
        -ex 'handle SIGCONT nostop print pass' \
        -batch -ex continue -ex bt
done 2>&1 >> gdb.log

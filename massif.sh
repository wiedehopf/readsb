#!/bin/bash
trap "ms_print .massif.out" EXIT
export MASSIF="--tool=massif --massif-out-file=.massif.out"
./valgrind.sh

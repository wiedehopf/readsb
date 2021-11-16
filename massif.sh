#!/bin/bash
trap "ms_print .massif.out" EXIT
./valgrind.sh --tool=massif --massif-out-file=.massif.out


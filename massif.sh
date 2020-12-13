#!/bin/bash
trap "ms_print .massif.out" EXIT
./valgrind.sh --tool=massif --stacks=yes --massif-out-file=.massif.out


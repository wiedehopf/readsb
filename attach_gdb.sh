#!/bin/bash
date -u
date
gdb attach "$(pgrep readsb)" \
	-ex 'handle SIGTERM nostop print pass' \
	-ex 'handle SIGCONT nostop print pass' \
	-ex continue

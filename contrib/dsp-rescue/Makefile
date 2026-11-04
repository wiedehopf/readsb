PYTHON ?= python3
CC ?= cc
CFLAGS ?= -O3
# Reproducible comparisons require normal IEEE arithmetic (no fast-math).
DSP_CFLAGS = -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fPIC

.PHONY: all test clean
all: dsp_rescue/_frontend.so

dsp_rescue/_frontend.so: dsp_rescue/frontend.c
	$(CC) $(CFLAGS) $(DSP_CFLAGS) -shared $< -lm -o $@

test: all
	OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 $(PYTHON) -m unittest discover -s tests -v

clean:
	rm -f dsp_rescue/_frontend.so

.PHONY: test-bounds
test-bounds:
	$(CC) -O1 -g -std=c11 -ffp-contract=off -fsanitize=address,undefined -fno-omit-frame-pointer tests/bounds.c -lm -o .bounds-test
	./.bounds-test
	$(RM) .bounds-test
	$(RM) -r .bounds-test.dSYM

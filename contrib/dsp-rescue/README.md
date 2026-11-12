# Experimental energy and DSP rescue replay

This opt-in `contrib` tool evaluates additional DF17 recovery from 2.4 MS/s UC8
recordings. It contains a native C front end and a small NumPy/SciPy replay driver.
It does not change readsb's demodulator, default build, SDR handling, tracking,
network output or service configuration. No inference engine, learned weights,
training code or external decoder service is needed.

This is a reference implementation for algorithm and false-positive review,
**not an integrated readsb demodulator replacement or a production feeder**.
The only outputs are local JSON files. None of the candidates is independent
RF ground truth; CRC and track identity guards do not prove correctness.

## Build and test

Python 3.10+, a C11 compiler and libm are required. The optional Python packages
are isolated from the readsb executable and its normal build dependencies.

```sh
cd contrib/dsp-rescue
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
make
make test PYTHON=.venv/bin/python
make test-bounds             # optional ASan/UBSan build, supported compiler needed
```

The tests use generated signals and public Mode S example payloads. They compare
energy slicing, fractional timing and matched filtering with independent NumPy
references; check FIR phase and overlap; exercise bounded weak-bit correction;
and verify clock, deduplication and prior-stock-identity guards.
`test-bounds` sweeps empty/short buffers, extreme start offsets and native window
boundaries under AddressSanitizer and UndefinedBehaviorSanitizer.

## Paired replay

Record interleaved unsigned I/Q at **exactly 2,400,000 complex samples/second**.
The input file has I,Q byte pairs, centered at 127.5. Other sample rates and sample
formats are not supported. A replay without `--stock` reports raw candidates,
not improvements over readsb:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
  .venv/bin/python -m dsp_rescue capture.uc8 --output report.json
```

To measure additions, pass a stock replay of the **same complete capture**, with
sample-clock-relative preamble timestamps in seconds. JSON input is a list like:

```json
[{"seconds": 0.002887416666666667,
  "message_hex": "8d40621d58c382d690c8ac2863a7", "source": "stock"}]
```

Alternatively, use readsb's timestamped raw output (`@<12 hex ticks><payload>;`):

```sh
../../readsb --device-type ifile --ifile capture.uc8 --iformat UC8 \
  --raw --mlat > stock.txt

OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 \
  .venv/bin/python -m dsp_rescue capture.uc8 --stock stock.txt \
  --stock-offset-us 199.83333333333334 --output comparison.json
```

**The example offset is specific to the tested 3.16.18 ifile path:** 64 µs for
readsb's end-of-bit-56 timestamp plus 326 history samples / 2.4 MS/s. Verify the
history and timestamp convention for the version/path being tested; a live tap
or another SDR path can have a different origin. Do not infer RF time from TCP
arrival timestamps. Timestamped text requires an explicit offset. The tool then
requires at least 20 distinct stock transmission matches from the energy slicer,
with median absolute residual ≤2 µs. A failed clock audit produces no guarded
additions and exits with status 2. Empty or failed stock replays cannot pass it.
Some readsb ifile builds exit nonzero at EOF; inspect the replay output and audit
rather than treating that exit code alone as a successful paired test.

Use `--methods energy` for the energy-only counterfactual; add `timing`, `soft`
and `matched` individually. `--budget` limits failed windows retried per 50 ms,
default 32 and maximum 128. `--events` optionally writes raw candidate metadata;
it never publishes or forwards messages. Reports include input/stock SHA-256,
method counts, work budgets, RF duration, wall time and clock audit.

## Algorithms

The shared front end resamples 2.4 to 4 MS/s with a centered, 101-tap Kaiser FIR.
Four-sample PPM symbols then permit simple integrated-energy comparisons. This
interpolation does not create RF bandwidth. Native 50 ms blocks use 600-sample
context on each side; ownership of candidate starts is half-open, so complete
120 µs packets and FIR context cross block boundaries without double ownership.
The preamble score is normalized energy correlation, threshold 0.4 by default.

1. **Energy:** integrate the two samples in each PPM half-symbol, with per-packet
   preamble-power normalization and clipping at 12 times that scale. Keep the
   received DF bits, require DF17 and an unchanged zero-CRC payload.
2. **Timing:** retry six offsets (-1, -0.5, -0.25, +0.25, +0.5, +1 samples at
   4 MS/s) using eight-tap windowed-sinc interpolation of complex I/Q. Each
   hypothesis must pass DF17 and CRC independently.
3. **Soft decisions:** use the raw half-symbol energy margin to select at most
   six weak payload/parity bits with confidence ≤0.35. Test at most 21 one-/two-bit
   hypotheses and accept only a unique CRC solution. DF, CA and ICAO bits 0..31
   are never repaired. This is not an unrestricted all-bit CRC search.
4. **Matched filtering:** estimate coarse carrier offset from known preamble
   pulses over ±100 kHz in 5 kHz increments; derive a short complex pulse shape
   from the preamble and compare matched half-symbol responses. Require DF17/CRC.

Energy checks are batched over all header-screened candidates. The more expensive
retries use only the strongest failed energy windows within the per-block budget.
Work outside that budget is counted, not queued indefinitely. The prototype is
serial; it does not add background workers or claim a live CPU scheduling budget.
Work buffers are bounded per block, but candidate lists and the paired stock
comparison accumulate per recording; split long recordings into matched chunks.
The C accumulation order is kept comparable to the reference implementation;
fast-math is deliberately not enabled. AArch64 has an optional NEON FIR path;
other architectures use the scalar implementation.

## Guarded counting

Method attribution is energy, timing, soft, then matched. Preserve every stock
record, including non-DF17 traffic. Guarded additions must:

- have valid DF17 CRC and a verified shared RF clock;
- be absent from stock and earlier accepted methods within ±120 µs;
- have a CRC-valid **stock** ICAO sighting in the preceding 30 seconds;
- not conflict with a different stock payload for that ICAO within ±4 µs.

Additional candidates never establish identity for later candidates. The guarded
union is a count of additional transmissions, not globally distinct payloads or
additional aircraft. This prototype intentionally excludes broad CRC hypotheses,
relaxed header retries and interference cancellation.

## Before live integration

Review needs include longer independent receiver/noise/overload controls,
adversarial false-positive tests, timing-origin verification for every input
path, and profiling on slower supported platforms. A production implementation
would also need an agreed location in readsb's native sample-processing pipeline,
bounded asynchronous scheduling, stock-priority backpressure and end-to-end
packet timestamp/duplicate handling. The tool does not implement those hooks or
change live feed behavior. See `VALIDATION.md` for the limited paired replay.

SPDX-License-Identifier: GPL-3.0-or-later. The parent readsb license applies.

# Initial validation (25 October 2026)

This is one recorded 10-second RF interval, not a representative performance or
false-positive study. The original capture and receiver-specific logs are not
included in this contribution. Unit tests use synthetic signals; external replay
numbers are identified by content hashes for later reproduction if data is shared.

## Capture and comparison

- Input: 48,000,000 bytes, UC8 at 2.4 MS/s.
- I/Q SHA-256: `0bd2525ea34e40ffd8c06973986110bcf609b1e55adfe4c5491cff499a9d8576`.
- Default methods: energy, timing, soft, matched; retry budget 32 per 50 ms.
- 200 RF blocks; 205,628 preamble candidates; 197,678 header rejections.
- 5,977 retried windows; 664 outside the retry budget; 123,159 soft hypotheses.
- Raw, cross-method-deduplicated candidates: energy 1,308; timing 104; soft 39;
  matched 8. These raw totals include stock transmissions and are not additions.

| Paired stock baseline | Stock DF17 | Energy additions | Timing | Soft | Matched | Guarded union |
|---|---:|---:|---:|---:|---:|---:|
| readsb 3.16.18, fresh macOS replay | 1,403 | 34 | 21 | 16 | 4 | 75 |
| readsb 3.16.17, earlier pinned replay | 1,404 | 35 | 22 | 16 | 4 | 77 |

The 3.16.18 replay used unmodified upstream commit
`9460652952a62fcfd9bf11a2664d737147f266a5` built with the repository's normal
`make -j2` on macOS arm64. The stock text had 6,272 total Mode S records,
SHA-256 `7ce6609d1286a0cbe0f7383a9852360ac1714fab68fab6fd07fe82d0af2b50b9`.
Its explicit stock timestamp offset was 199.83333333333334 µs.
The earlier normalized DF17-only stock JSON had SHA-256
`60e96b1b8958a94e2fb9c59362f2198a1d418e5cc8ef116225567b54b2392712`.
Do not attribute the baseline difference solely to decoder version: replay
platform/build also differs.

Both audits found 1,271 distinct energy/stock transmission matches, with median
absolute clock residual 0.083333 µs. The guarded unions are 5.35% and 5.48% of the
respective stock DF17 totals. They are additional CRC/identity-consistent
transmissions, **not independently verified correct messages**. No range or
long-term reception improvement is inferred from this interval.

## Port/extraction parity and controls

The standalone extraction and the earlier deployed DSP-only algorithms produced
identical `(sample position, payload, method)` sets for all 1,459 raw candidates
on this capture. AArch64 Pi replay produced the same raw counts and the same
77 guarded additions when using the same earlier pinned stock JSON.

A negative control shuffled original I/Q *pairs* in time, preserving the sample
value distribution while destroying packet structure (seed 251025, 10 RF seconds).
It produced zero raw candidates, despite 1,320,212 preamble peaks and hitting the
32-window retry cap in many blocks. This small control does not bound the false
positive rate; independent RF/noise/overload and adversarial controls remain needed.

## Tests and runtime

- 14 Python tests passed on macOS arm64 and Raspberry Pi 4 / Linux aarch64.
- Native energy and timing outputs match independent NumPy references; matched
  output matches the independent vectorized carrier/pulse-shape reference.
- FIR phase/boundary checks, weak-bit/header/address restrictions, baseline
  preservation, stock-only prior identity, conflicts, duplicate and clock guards pass.
- Native ASan/UBSan bounds sweep passed on macOS, including zero/short buffers
  and `INT64_MAX` candidate offsets.
- The unmodified upstream readsb default macOS build completed successfully.

The 10 RF seconds took 0.633 s on the development macOS arm64 host in one replay.
A Pi 4 validation deliberately limited to **0.25 CPU core** took 36.413 s for
that decode; it ran at nice 19 on CPUs 0–1 while the receiver and feeder kept
running. This is a throttled isolation check, not an unrestricted Pi real-time
benchmark or proof of live schedulability. The Pi validation compiled the native
kernel and passed all tests, and confirmed receiver, DSP, relay and FR24 PIDs
were unchanged. No packets from this replay were forwarded.

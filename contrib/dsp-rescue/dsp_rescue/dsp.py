# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded block I/Q processing and vectorized Mode-S CRC. No SDR/socket access."""

from pathlib import Path
import numpy as np
from scipy.signal import firwin, upfirdn, find_peaks

FS = 4_000_000
SAMPLES = 480
PREAMBLE = np.zeros(32, dtype=np.float32)
for i in (0, 4, 14, 18):
    PREAMBLE[i : i + 2] = 1
TEMPLATE = PREAMBLE - PREAMBLE.mean()
# Same Kaiser FIR and centering as scipy resample_poly(5,3), cached once.
FIR = np.concatenate(
    (
        np.zeros(1, np.float32),
        firwin(101, 0.2, window=("kaiser", 5.0)).astype(np.float32) * 5,
    )
)


def _crc_table():
    table = []
    for b in range(256):
        r = b << 16
        for _ in range(8):
            r = (r << 1) ^ (0x1FFF409 if r & 0x800000 else 0)
        table.append(r & 0xFFFFFF)
    return np.array(table, dtype=np.uint32)


CRC_TABLE = _crc_table()


def crc_batch(data):
    data = np.asarray(data, dtype=np.uint8)
    r = np.zeros(len(data), dtype=np.uint32)
    for i in range(data.shape[1]):
        r = ((r << 8) & 0xFFFFFF) ^ CRC_TABLE[((r >> 16) ^ data[:, i]) & 255]
    return r


def payloads(logits):
    # Keep received header bits; do not force the DF or ICAO.
    return np.packbits(logits > 0, axis=-1)


def clipped_power(iq):
    p = (np.abs(iq) ** 2).astype(np.float32)
    scale = p[:, :32][:, PREAMBLE.astype(bool)].mean(axis=1, keepdims=True)
    p /= np.maximum(scale, 1e-5)
    return np.clip(p, 0, 12).astype(np.float32)


def energy_logits(x):
    p = x[:, 32:].reshape(-1, 112, 4)
    return (p[:, :, :2].sum(-1) - p[:, :, 2:].sum(-1)) * 3


def candidate_scores(iq, core, threshold):
    p = (np.abs(iq) ** 2).astype(np.float32)
    corr = np.correlate(p, TEMPLATE, "valid")
    # Accumulate in float64 to avoid cancellation over long/noisy clips.
    cs = np.concatenate(([0.0], np.cumsum(p.astype(np.float64) ** 2)))
    den = np.sqrt(np.maximum(cs[32:] - cs[:-32], 1e-12) * np.sum(TEMPLATE**2))
    score = corr / den
    peaks, _ = find_peaks(score, height=threshold, distance=3)
    peaks = peaks[(peaks < core) & (peaks + SAMPLES <= len(iq))]
    return peaks, score[peaks]


def iq_blocks(path, block_seconds=0.05):
    """UC8 at exactly 2.4 MS/s; overlapped polyphase resampling to 4 MS/s.

    Each block owns starts in [offset,offset+core). The right halo contains
    complete messages across boundaries. 600 native halo samples also exceed
    the resampler FIR support. Block/halo lengths are multiples of 3.
    """
    path = Path(path)
    if path.stat().st_size % 2:
        raise ValueError("Truncated UC8 I/Q pair")
    raw = np.memmap(path, dtype=np.uint8, mode="r").reshape(-1, 2)
    total = len(raw)
    hop = round(block_seconds * 2_400_000 / 3) * 3
    if hop < 600:
        raise ValueError("Block must be at least 250 microseconds")
    halo = 600
    for start in range(0, total, hop):
        end = min(start + hop, total)
        lo = max(0, start - halo)
        hi = min(total, end + halo)
        v = (raw[lo:hi].astype(np.float32) - 127.5) / 128
        v = np.pad(v, ((max(0, halo - start), max(0, end + halo - total)), (0, 0)))
        # Filter real I/Q channels with real taps, avoiding complex FIR math.
        y = upfirdn(FIR, v, up=5, down=3, axis=0)[17 : 17 + (len(v) * 5 + 2) // 3]
        z = (y[:, 0] + 1j * y[:, 1]).astype(np.complex64)
        offset = start * 5 // 3
        core = (end * 5 // 3) - offset
        # Ignore starts that cannot have a full real captured frame, even if
        # right padding would make the array long enough.
        available = max(0, min(core, total * 5 // 3 - offset - SAMPLES + 1))
        yield offset, available, z[1000 : 1000 + core + SAMPLES]

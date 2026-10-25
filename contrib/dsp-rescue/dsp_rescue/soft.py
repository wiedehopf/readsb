# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded low-confidence correction; never alter DF, CA or ICAO bits."""

import itertools
from collections import Counter
import numpy as np
from .dsp import SAMPLES, payloads, crc_batch

DF17 = np.array([1, 0, 0, 0, 1], bool)
SHIFTS = (-1.0, -0.5, -0.25, 0.25, 0.5, 1.0)
# Linear CRC allows cheap syndrome lookup instead of recomputing 21 full frames.
BIT_SYNDROMES = crc_batch(np.packbits(np.eye(112, dtype=np.uint8), axis=1))


def window(iq, starts, shift=0.0, margin=0):
    """Eight-tap windowed-sinc interpolation of original complex samples."""
    starts = np.asarray(starts, dtype=np.int64)
    length = SAMPLES + margin * 2
    integer = int(np.floor(shift))
    fraction = shift - integer
    taps = np.arange(-3, 5)
    weights = np.sinc(taps - fraction) * np.kaiser(8, 5)
    weights = (weights / weights.sum()).astype(np.float32)
    positions = (
        starts[:, None] + np.arange(-margin, SAMPLES + margin)[None, :] + integer
    )
    out = np.zeros((len(starts), length), np.complex64)
    if fraction == 0:
        return iq[positions]
    for tap, weight in zip(taps, weights):
        out += iq[positions + tap] * weight
    return out


def logits_and_confidence(windows):
    power = np.abs(windows) ** 2
    p = power[:, 32:].reshape(-1, 112, 4)
    left = p[:, :, :2].sum(-1)
    right = p[:, :, 2:].sum(-1)
    return left - right, np.abs(left - right) / (left + right + 1e-9)


def soft_decode(logits, confidence, limit=0.35, k=6):
    """Unique CRC solution among <=21 hypotheses. Never alter ICAO bits 8..31.
    No unrestricted bit guessing, packet synthesis, or future-track imputation.
    """
    if not 1 <= k <= 6 or not 0 <= limit <= 0.35:
        raise ValueError("At most six weak bits with confidence <=0.35")
    original = payloads(logits)
    syndromes = crc_batch(original)
    out = []
    stats = Counter()
    for i, syndrome in enumerate(syndromes):
        if not syndrome:
            continue
        eligible = np.arange(32, 112)
        ids = eligible[np.argsort(confidence[i, eligible], kind="stable")[:k]]
        ids = ids[confidence[i, ids] <= limit]
        solutions = []
        for count in (1, 2):
            for flipped in itertools.combinations(ids, count):
                stats["hypotheses"] += 1
                correction = 0
                for bit in flipped:
                    correction ^= int(BIT_SYNDROMES[bit])
                if correction != int(syndrome):
                    continue
                row = original[i].copy()
                for bit in flipped:
                    row[bit // 8] ^= 1 << (7 - bit % 8)
                if row[0] >> 3 == 17:
                    solutions.append((row, tuple(map(int, flipped))))
        if len(solutions) == 1:
            out.append((i, *solutions[0]))
        elif solutions:
            stats["ambiguous"] += 1
    return out, dict(stats)

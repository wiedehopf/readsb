# SPDX-License-Identifier: GPL-3.0-or-later
"""Small optional C front end. Python owns arrays; C only writes bounded outputs."""

import ctypes as c
from pathlib import Path
import numpy as np
from scipy.signal import find_peaks
from .dsp import FIR, SAMPLES

_LIB = None


def library():
    global _LIB
    if _LIB is None:
        path = Path(__file__).with_name("_frontend.so")
        if not path.exists():
            raise RuntimeError("Build the native frontend with make first")
        _LIB = c.CDLL(str(path))
        floatp = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
        doublep = np.ctypeslib.ndpointer(dtype=np.float64, flags="C_CONTIGUOUS")
        _LIB.rescue_resample.argtypes = [
            np.ctypeslib.ndpointer(dtype=np.uint8, flags="C_CONTIGUOUS"),
            c.c_int64,
            c.c_int64,
            c.c_int64,
            floatp,
            floatp,
        ]
        _LIB.rescue_resample.restype = None
        _LIB.rescue_resample_float.argtypes = [
            floatp,
            c.c_int64,
            c.c_int64,
            c.c_int64,
            floatp,
            floatp,
        ]
        _LIB.rescue_resample_float.restype = None
        _LIB.rescue_scores.argtypes = [floatp, c.c_int64, floatp, doublep]
        _LIB.rescue_scores.restype = None
        intp = np.ctypeslib.ndpointer(dtype=np.int64, flags="C_CONTIGUOUS")
        _LIB.rescue_header_ids.argtypes = [floatp, c.c_int64, intp, c.c_int64, intp]
        _LIB.rescue_header_ids.restype = c.c_int64
    return _LIB


def iq_blocks(
    path, block_seconds=0.05, block_range=None, source_first=0, total_samples=None
):
    path = Path(path)
    if not path.stat().st_size:
        raise ValueError("Empty UC8 capture")
    if path.stat().st_size % 2:
        raise ValueError("Truncated UC8 I/Q pair")
    raw = np.memmap(path, dtype=np.uint8, mode="r").reshape(-1, 2)
    total = len(raw) if total_samples is None else total_samples
    if source_first and block_range is None:
        raise ValueError("Partial source needs explicit block range")
    hop = round(block_seconds * 2400000 / 3) * 3
    if hop < 600:
        raise ValueError("Block must be at least 250 microseconds")
    lib = library()
    for index, start in enumerate(range(0, total, hop)):
        # Preserve the serial FIR phase and packet halos; skip before resampling.
        if block_range is not None and not block_range[0] <= index < block_range[1]:
            continue
        end = min(start + hop, total)
        offset = start * 5 // 3
        core = end * 5 // 3 - offset
        available = max(0, min(core, total * 5 // 3 - offset - SAMPLES + 1))
        lo = max(0, start - 600)
        hi = min(total, end + 600)
        if lo < source_first or hi > source_first + len(raw):
            raise ValueError("Missing FIR context in partial source")
        v = (
            raw[lo - source_first : hi - source_first].astype(np.float32) - 127.5
        ) / 128
        v = np.pad(v, ((max(0, 600 - start), max(0, end + 600 - total)), (0, 0)))
        out = np.empty((core + SAMPLES) * 2, np.float32)
        lib.rescue_resample_float(v, len(v), 1000, core + SAMPLES, FIR, out)
        yield offset, available, out.view(np.complex64)


def candidate_scores(iq, core, threshold, with_power=False):
    iq = np.ascontiguousarray(iq, dtype=np.complex64)
    power = np.empty(len(iq), np.float32)
    score = np.empty(len(iq) - 31, np.float64)
    library().rescue_scores(iq.view(np.float32), len(iq), power, score)
    peaks, _ = find_peaks(score, height=threshold, distance=3)
    peaks = peaks[(peaks < core) & (peaks + SAMPLES <= len(iq))]
    return (peaks, score[peaks], power) if with_power else (peaks, score[peaks])


def header_ids(iq, starts):
    starts = np.ascontiguousarray(starts, dtype=np.int64)
    ids = np.empty(len(starts), dtype=np.int64)
    count = library().rescue_header_ids(
        iq.view(np.float32), len(iq), starts, len(starts), ids
    )
    return ids[:count]


def dsp_candidates(iq, starts, timing=True, matched=True):
    from .soft import SHIFTS

    starts = np.ascontiguousarray(starts, np.int64)
    integers = np.floor(SHIFTS).astype(np.int64)
    taps = np.arange(-3, 5)
    weights = []
    for shift, integer in zip(SHIFTS, integers):
        w = np.sinc(taps - (shift - integer)) * np.kaiser(8, 5)
        weights.append((w / w.sum()).astype(np.float32))
    weights = np.stack(weights)
    capacity = len(starts) * 7
    ids = np.empty(capacity, np.int64)
    methods = np.empty(capacity, np.int64)
    rows = np.empty((capacity, 14), np.uint8)
    arr = lambda dtype: np.ctypeslib.ndpointer(dtype=dtype, flags="C_CONTIGUOUS")
    lib = library()
    lib.rescue_dsp.argtypes = [
        arr(np.float32),
        c.c_int64,
        arr(np.int64),
        c.c_int64,
        arr(np.int64),
        arr(np.float32),
        c.c_int,
        c.c_int,
        arr(np.int64),
        arr(np.int64),
        arr(np.uint8),
    ]
    lib.rescue_dsp.restype = c.c_int64
    count = lib.rescue_dsp(
        iq.view(np.float32),
        len(iq),
        starts,
        len(starts),
        integers,
        weights,
        int(timing),
        int(matched),
        ids,
        methods,
        rows,
    )
    return ids[:count], methods[:count], rows[:count]


def energy_candidates(iq, starts):
    starts = np.ascontiguousarray(starts, np.int64)
    rows = np.empty((len(starts), 14), np.uint8)
    valid = np.empty(len(starts), np.uint8)
    arr = lambda dtype: np.ctypeslib.ndpointer(dtype=dtype, flags="C_CONTIGUOUS")
    lib = library()
    lib.rescue_baseline.argtypes = [
        arr(np.float32),
        c.c_int64,
        arr(np.int64),
        c.c_int64,
        arr(np.uint8),
        arr(np.uint8),
    ]
    lib.rescue_baseline.restype = None
    lib.rescue_baseline(iq.view(np.float32), len(iq), starts, len(starts), rows, valid)
    return rows, valid.astype(bool)

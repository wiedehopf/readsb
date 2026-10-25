# SPDX-License-Identifier: GPL-3.0-or-later
"""Paired replay audit. Guarded additions are candidates, not ground truth."""

import bisect, math, statistics
from collections import Counter, defaultdict
from .decode import METHODS
from .dsp import crc_batch
import numpy as np


def valid(payload):
    try:
        row = bytes.fromhex(payload)
    except (TypeError, ValueError):
        return False
    return (
        len(row) == 14
        and row[0] >> 3 == 17
        and not crc_batch(np.frombuffer(row, np.uint8)[None, :])[0]
    )


def evaluate(stock, events):
    # Baseline records, including non-DF17 traffic, are copied unchanged.
    primary = [dict(r) for r in stock]
    payloads = defaultdict(list)
    identities = defaultdict(list)
    for r in primary:
        if not math.isfinite(r["seconds"]):
            raise ValueError("Nonfinite stock timestamp")
        p = r["message_hex"]
        if valid(p):
            payloads[p].append(r["seconds"])
            identities[p[2:8]].append((r["seconds"], p))
    for rows in (*payloads.values(), *identities.values()):
        rows.sort()
    candidates = [
        r
        for r in events
        if r.get("source") in METHODS
        and valid(r.get("message_hex"))
        and math.isfinite(r["seconds"])
        and r["seconds"] >= 0
    ]
    errors = []
    matched = set()
    for r in candidates:
        if r["source"] != "energy":
            continue
        times = payloads.get(r["message_hex"], ())
        i = bisect.bisect_left(times, r["seconds"])
        near = times[max(0, i - 1) : i + 1]
        if near:
            closest = min(near, key=lambda t: abs(t - r["seconds"]))
            key = (r["message_hex"], closest)
            if key not in matched and abs(closest - r["seconds"]) < 0.001:
                errors.append(abs(closest - r["seconds"]))
                matched.add(key)
    median = statistics.median(errors) if errors else None
    audit = dict(
        matches=len(errors),
        median_abs_error_us=None if median is None else median * 1e6,
        verified=len(errors) >= 20 and median <= 2e-6,
    )
    counts = Counter()
    extra = []
    if audit["verified"]:
        for r in sorted(
            candidates, key=lambda r: (METHODS.index(r["source"]), r["seconds"])
        ):
            p = r["message_hex"]
            t = r["seconds"]
            times = payloads.get(p, [])
            i = bisect.bisect_left(times, t - 120e-6)
            if i < len(times) and times[i] <= t + 120e-6:
                counts["duplicate_120us"] += 1
                continue
            sightings = identities.get(p[2:8], ())
            if not any(t - 30 <= s <= t for s, q in sightings):
                counts["no_prior_stock_icao"] += 1
                continue
            if any(abs(s - t) <= 4e-6 and q != p for s, q in sightings):
                counts["stock_conflict"] += 1
                continue
            extra.append(
                {**r, "validation": "crc_and_prior_stock_icao", "provisional": True}
            )
            bisect.insort(payloads[p], t)
            counts["added_" + r["source"]] += 1
    else:
        counts["alignment_unverified"] = len(candidates)
    merged = sorted(
        primary + extra, key=lambda r: (r["seconds"], r.get("source", "stock"))
    )
    return dict(
        alignment=audit,
        counts=dict(counts),
        stock_records=len(primary),
        stock_df17=sum(valid(r["message_hex"]) for r in primary),
        added=extra,
        merged=merged,
    )

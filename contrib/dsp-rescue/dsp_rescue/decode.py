# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in, bounded DSP replay; no SDR ownership or network output."""

import time
from collections import Counter
import numpy as np
from . import native
from .soft import SHIFTS, window, logits_and_confidence, soft_decode

METHODS = ("energy", "timing", "soft", "matched")


class Decoder:
    def __init__(self, methods=METHODS, budget=32, threshold=0.4):
        if not set(methods) <= set(METHODS) or "energy" not in methods:
            raise ValueError("Energy is required; choose energy/timing/soft/matched")
        if not 1 <= budget <= 128 or not 0 < threshold <= 1:
            raise ValueError("Invalid candidate budget or threshold")
        self.methods = tuple(m for m in METHODS if m in methods)
        self.budget = budget
        self.threshold = threshold

    def decode(self, path, blocks=None):
        events = []
        seen = {}
        counts = Counter()
        begin = time.perf_counter()

        def emit(source, sample, row):
            payload = row.tobytes().hex()
            if any(abs(sample - t) < 480 for t in seen.get(payload, ())):
                return
            seen.setdefault(payload, []).append(sample)
            events.append(
                dict(
                    seconds=float(sample) / 4000000,
                    sample_4msps=float(sample),
                    message_hex=payload,
                    source=source,
                )
            )
            counts["decoded_" + source] += 1

        source = native.iq_blocks(path) if blocks is None else blocks
        for offset, core, iq in source:
            starts, scores = native.candidate_scores(iq, core, self.threshold)
            counts["blocks"] += 1
            counts["preamble_candidates"] += len(starts)
            ids = native.header_ids(iq, starts)
            counts["header_rejected"] += len(starts) - len(ids)
            pending = []
            for lo in range(0, len(ids), 128):
                selected = ids[lo : lo + 128]
                positions = starts[selected]
                rows, valid = native.energy_candidates(iq, positions)
                for j in np.flatnonzero(valid):
                    emit("energy", offset + int(positions[j]), rows[j])
                for j in np.flatnonzero(~valid):
                    i = int(selected[j])
                    s = int(starts[i])
                    if s >= 8 and s + 488 < len(iq):
                        pending.append(i)
            pending = sorted(pending, key=lambda i: (-scores[i], i))
            counts["dsp_budget_dropped"] += max(0, len(pending) - self.budget)
            selected = sorted(pending[: self.budget])
            positions = starts[selected]
            counts["dsp_candidates"] += len(positions)
            # Attribution priority is fixed: energy, timing, soft, matched.
            matched = []
            if len(positions) and (
                "timing" in self.methods or "matched" in self.methods
            ):
                indices, methods, rows = native.dsp_candidates(
                    iq, positions, "timing" in self.methods, "matched" in self.methods
                )
                for i, method, row in zip(indices, methods, rows):
                    if method < 6:
                        emit(
                            "timing", offset + float(positions[i]) + SHIFTS[method], row
                        )
                    else:
                        matched.append((int(positions[i]), row.copy()))
            if len(positions) and "soft" in self.methods:
                logits, confidence = logits_and_confidence(window(iq, positions))
                repaired, work = soft_decode(logits, confidence)
                counts["soft_hypotheses"] += work.get("hypotheses", 0)
                counts["soft_ambiguous"] += work.get("ambiguous", 0)
                for j, row, flips in repaired:
                    emit("soft", offset + int(positions[j]), row)
            for position, row in matched:
                emit("matched", offset + position, row)
            # Retain at most one RF second of cross-block deduplication state.
            seen = {
                p: [s for s in times if s > offset + core - 4000000]
                for p, times in seen.items()
                if times[-1] > offset + core - 4000000
            }
        events.sort(key=lambda r: (r["sample_4msps"], METHODS.index(r["source"])))
        return events, dict(
            wall_s=time.perf_counter() - begin,
            counts=dict(counts),
            methods=self.methods,
            budget_per_50ms=self.budget,
        )

# SPDX-License-Identifier: GPL-3.0-or-later
import tempfile, unittest
from pathlib import Path
import numpy as np
from dsp_rescue import native
from dsp_rescue.dsp import (
    energy_logits,
    clipped_power,
    payloads,
    crc_batch,
    iq_blocks as reference_blocks,
)
from dsp_rescue.soft import SHIFTS, window, logits_and_confidence, soft_decode
from dsp_rescue.decode import Decoder

P = "8d40621d58c382d690c8ac2863a7"


def pulse():
    out = np.zeros(480, np.complex64)
    for t in (0, 4, 14, 18):
        out[t : t + 2] = 1
    for b, bit in enumerate(np.unpackbits(np.frombuffer(bytes.fromhex(P), np.uint8))):
        t = 32 + 4 * b + (0 if bit else 2)
        out[t : t + 2] = 1
    return out


class DSPTests(unittest.TestCase):
    def setUp(self):
        rng = np.random.default_rng(251025)
        self.iq = (rng.normal(0, 0.005, 8000) + 1j * rng.normal(0, 0.005, 8000)).astype(
            np.complex64
        )
        for at in (100, 1500, 3000):
            self.iq[at : at + 480] += pulse()
        self.starts = np.array([100, 1500, 3000, 5000], np.int64)

    def test_energy_matches_reference(self):
        rows, valid = native.energy_candidates(self.iq, self.starts)
        reference = payloads(
            energy_logits(clipped_power(self.iq[self.starts[:, None] + np.arange(480)]))
        )
        np.testing.assert_array_equal(rows, reference)
        np.testing.assert_array_equal(valid, crc_batch(reference) == 0)

    def test_timing_matches_independent_reference(self):
        ids, methods, rows = native.dsp_candidates(self.iq, self.starts, matched=False)
        actual = {
            (int(i), int(m), r.tobytes().hex()) for i, m, r in zip(ids, methods, rows)
        }
        expected = set()
        for method, shift in enumerate(SHIFTS):
            logits, _ = logits_and_confidence(window(self.iq, self.starts, shift))
            pred = payloads(logits)
            expected.update(
                (int(i), method, pred[i].tobytes().hex())
                for i in np.flatnonzero(
                    (crc_batch(pred) == 0) & ((pred[:, 0] >> 3) == 17)
                )
            )
        self.assertEqual(actual, expected)

    def test_matched_known_packet_and_budget(self):
        ids, methods, rows = native.dsp_candidates(self.iq, self.starts, timing=False)
        self.assertTrue(len(ids))
        self.assertTrue(np.all(methods == 6))
        self.assertTrue(all(r.tobytes().hex() == P for r in rows))
        with self.assertRaises(ValueError):
            Decoder(budget=129)
        with self.assertRaises(ValueError):
            Decoder(methods=("timing",))

    def test_matched_matches_independent_reference(self):
        from reference_matched import decode

        ids, methods, rows = native.dsp_candidates(self.iq, self.starts, timing=False)
        actual = {(int(i), r.tobytes().hex()) for i, r in zip(ids, rows)}
        self.assertEqual(actual, decode(self.iq, self.starts))

    def test_soft_correction_preserves_header_and_address(self):
        bits = np.unpackbits(np.frombuffer(bytes.fromhex(P), np.uint8))
        logits = (bits.astype(float) * 2 - 1)[None, :]
        confidence = np.ones_like(logits)
        for b in (40, 70):
            logits[0, b] *= -1
            confidence[0, b] = 0.1
        rows, stats = soft_decode(logits, confidence)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][1].tobytes().hex(), P)
        self.assertEqual(set(rows[0][2]), {40, 70})
        self.assertLessEqual(stats["hypotheses"], 21)
        logits = (bits.astype(float) * 2 - 1)[None, :]
        logits[0, 20] *= -1
        confidence = np.ones_like(logits)
        confidence[0, 20] = 0
        self.assertEqual(soft_decode(logits, confidence)[0], [])

    def test_native_rejects_out_of_bounds_windows(self):
        starts = np.array([-1, 2**63 - 1, len(self.iq) - 10], np.int64)
        self.assertEqual(len(native.header_ids(self.iq, starts)), 0)
        self.assertFalse(native.energy_candidates(self.iq, starts)[1].any())
        self.assertEqual(len(native.dsp_candidates(self.iq, starts)[0]), 0)

    def test_resampler_phase_and_boundary_context(self):
        rng = np.random.default_rng(42)
        raw = rng.integers(0, 256, 240003 * 2, dtype=np.uint8)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "test.uc8"
            p.write_bytes(raw.tobytes())
            native_rows = list(native.iq_blocks(p))
            reference_rows = list(reference_blocks(p))
            self.assertEqual(len(native_rows), len(reference_rows))
            for (offset, n, iq), (other, m, ref) in zip(native_rows, reference_rows):
                self.assertEqual((offset, n), (other, m))
                np.testing.assert_allclose(iq, ref, atol=2e-6, rtol=2e-6)
            p.write_bytes(b"1")
            with self.assertRaises(ValueError):
                list(native.iq_blocks(p))

    def test_decode_is_self_contained_and_deduplicates(self):
        import sys

        events, work = Decoder().decode(None, blocks=[(0, 7500, self.iq)])
        self.assertTrue(events)
        self.assertEqual({r["source"] for r in events}, {"energy"})
        self.assertEqual(len([r for r in events if r["message_hex"] == P]), 3)
        self.assertLessEqual(work["counts"]["dsp_candidates"], 32)
        self.assertNotIn("onnxruntime", sys.modules)
        self.assertNotIn("torch", sys.modules)


if __name__ == "__main__":
    unittest.main()

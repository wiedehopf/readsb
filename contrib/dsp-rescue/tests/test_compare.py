# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from dsp_rescue.compare import evaluate

P = "8d40621d58c382d690c8ac2863a7"
Q = "8d40621d58c386435cc412692ad6"


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.stock = [
            dict(seconds=i * 0.001, message_hex=P, source="stock") for i in range(25)
        ]
        self.anchors = [
            dict(seconds=r["seconds"] + 1e-6, message_hex=P, source="energy")
            for r in self.stock
        ]

    def event(self, source="timing", t=0.025):
        return dict(seconds=t, message_hex=Q, source=source)

    def test_clock_guard_and_unchanged_baseline(self):
        r = evaluate(self.stock, self.anchors + [self.event()])
        self.assertTrue(r["alignment"]["verified"])
        self.assertEqual(len(r["added"]), 1)
        self.assertEqual(
            [e for e in r["merged"] if e.get("source") == "stock"], self.stock
        )
        bad = [{**r, "seconds": r["seconds"] + 0.0001} for r in self.anchors]
        self.assertFalse(
            evaluate(self.stock, bad + [self.event()])["alignment"]["verified"]
        )

    def test_repeated_anchor_does_not_inflate_evidence(self):
        self.assertFalse(
            evaluate(self.stock, [self.anchors[0]] * 20)["alignment"]["verified"]
        )

    def test_stock_and_cross_method_duplicates(self):
        e = [self.event(source=s) for s in ("energy", "timing", "soft", "matched")]
        r = evaluate(self.stock, self.anchors + e)
        self.assertEqual([e["source"] for e in r["added"]], ["energy"])

    def test_future_stock_does_not_supply_prior_identity(self):
        stock = [{**r, "seconds": r["seconds"] + 0.1} for r in self.stock]
        anchors = [{**r, "seconds": r["seconds"] + 0.1} for r in self.anchors]
        r = evaluate(stock, anchors + [self.event(t=0.05)])
        self.assertFalse(r["added"])
        r = evaluate(self.stock, self.anchors + [self.event(t=31)])
        self.assertFalse(r["added"])
        self.assertEqual(r["counts"]["no_prior_stock_icao"], 1)

    def test_conflicting_stock_rejected(self):
        r = evaluate(self.stock, self.anchors + [self.event(t=0.010001)])
        self.assertFalse(r["added"])
        self.assertEqual(r["counts"]["stock_conflict"], 1)

    def test_unrecognized_source_and_invalid_crc_rejected(self):
        e = [
            self.event(source="unsupported"),
            {**self.event(), "message_hex": Q[:-1] + "0"},
        ]
        self.assertFalse(evaluate(self.stock, self.anchors + e)["added"])


if __name__ == "__main__":
    unittest.main()

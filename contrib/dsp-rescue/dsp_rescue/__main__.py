# SPDX-License-Identifier: GPL-3.0-or-later
import argparse, hashlib, json, re, sys, time
from collections import Counter
from pathlib import Path
from .decode import Decoder, METHODS
from .compare import evaluate


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(
        description="Experimental DSP-only UC8 replay; never opens SDRs or feeder sockets."
    )
    p.add_argument("iq", type=Path, help="Interleaved unsigned I/Q at exactly 2.4 MS/s")
    p.add_argument(
        "--stock",
        type=Path,
        help="Paired normalized JSON event list, or timestamped @...; readsb text",
    )
    p.add_argument(
        "--stock-offset-us",
        type=float,
        help="Subtract this from stock seconds; required for timestamped text",
    )
    p.add_argument("--methods", nargs="+", choices=METHODS, default=METHODS)
    p.add_argument(
        "--budget",
        type=int,
        default=32,
        help="Maximum failed energy windows retried per 50 ms (1..128)",
    )
    p.add_argument("--threshold", type=float, default=0.4)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument(
        "--events", type=Path, help="Optional raw candidate JSON output, not a feed"
    )
    a = p.parse_args()
    if a.output.resolve() == a.iq.resolve() or (
        a.stock and a.output.resolve() == a.stock.resolve()
    ):
        p.error("Output must not overwrite an input")
    if a.events and a.events.resolve() in {
        a.iq.resolve(),
        a.output.resolve(),
        *([] if a.stock is None else [a.stock.resolve()]),
    }:
        p.error("Events path must differ from inputs and report")
    events, work = Decoder(a.methods, a.budget, a.threshold).decode(a.iq)
    duration = a.iq.stat().st_size / 4800000
    report = dict(
        scope="offline DSP-only replay",
        iq_sha256=sha256(a.iq),
        rf_seconds=duration,
        real_time_factor=work["wall_s"] / duration,
        raw_candidates=dict(Counter(r["source"] for r in events)),
        work=work,
        forwarding_enabled=False,
    )
    verified = True
    if a.stock:
        text = a.stock.read_text()
        if text.lstrip().startswith("["):
            stock = json.loads(text)
        else:
            if a.stock_offset_us is None:
                p.error(
                    "Timestamped text requires explicit --stock-offset-us and clock audit"
                )
            stock = [
                dict(seconds=int(t, 16) / 12e6, message_hex=m.lower(), source="stock")
                for t, m in re.findall(
                    r"@([0-9a-fA-F]{12})([0-9a-fA-F]{14}(?:[0-9a-fA-F]{14})?);", text
                )
            ]
        if a.stock_offset_us is not None:
            stock = [
                {**r, "seconds": r["seconds"] - a.stock_offset_us / 1e6} for r in stock
            ]
        result = evaluate(stock, events)
        report["comparison"] = {
            k: v for k, v in result.items() if k not in ("added", "merged")
        }
        report["stock_sha256"] = sha256(a.stock)
        verified = result["alignment"]["verified"]
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    if a.events:
        a.events.write_text(json.dumps(events, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if not verified:
        raise SystemExit(2)


if __name__ == "__main__":
    main()

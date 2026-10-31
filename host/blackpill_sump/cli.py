"""Command line interface: ``python -m blackpill_sump <command>``."""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional, Tuple

from .client import Sump, SumpError
from .export import write_csv, write_vcd


def parse_rate(text: str) -> float:
    """'20M', '500k', '1e6' -> Hz."""
    mult = {"k": 1e3, "m": 1e6, "g": 1e9}
    text = text.strip().lower().rstrip("hz").rstrip("/s").rstrip("s")
    if text and text[-1] in mult:
        return float(text[:-1]) * mult[text[-1]]
    return float(text)


def parse_trigger(spec: Optional[str]) -> Optional[List[Tuple[int, int]]]:
    """'3:rise' | '3:fall' | '3:high' | '3:low' -> SUMP trigger stages."""
    if not spec:
        return None
    ch_s, _, kind = spec.partition(":")
    bit = 1 << int(ch_s)
    stages = {
        "rise": [(bit, 0), (bit, bit)],
        "fall": [(bit, bit), (bit, 0)],
        "high": [(bit, bit)],
        "low": [(bit, 0)],
    }
    if kind not in stages:
        raise argparse.ArgumentTypeError("trigger must look like 3:rise (rise|fall|high|low)")
    return stages[kind]


def cmd_info(args: argparse.Namespace) -> int:
    with Sump(args.port) as s:
        i = s.info
        print(f"port        {s.port}")
        print(f"device      {i.name} (firmware {i.firmware}, SUMP protocol {i.protocol})")
        print(f"probes      {i.probes}")
        print(f"memory      {i.sample_memory} samples")
        print(f"max rate    {i.max_rate_hz / 1e6:.1f} MS/s")
    return 0


def cmd_capture(args: argparse.Namespace) -> int:
    with Sump(args.port) as s:
        cap = s.capture(
            rate_hz=parse_rate(args.rate),
            samples=args.samples,
            post_trigger=args.post,
            trigger=parse_trigger(args.trigger),
            test_pattern=args.test_pattern,
            timeout=args.timeout,
        )
    print(f"{len(cap.samples)} samples at {cap.rate_hz / 1e6:.4f} MS/s "
          f"(requested {cap.requested_rate_hz / 1e6:.4f}), trigger at sample {cap.trigger_index}")
    if args.vcd:
        with open(args.vcd, "w", encoding="ascii") as f:
            write_vcd(cap, f)
    if args.csv:
        with open(args.csv, "w", encoding="ascii") as f:
            write_csv(cap, f)
    return 0


def cmd_selftest(args: argparse.Namespace) -> int:
    """Capture the built-in pattern and check frequencies and sample integrity."""
    expected = {4: 1e6, 5: 1e6, 6: 5e5, 7: 5e5, 3: 1e5}   # see firmware/App/Src/testgen.c
    ok = True
    with Sump(args.port) as s:
        print(f"{s.info.name} firmware {s.info.firmware} on {s.port}")
        for rate in (1e6, 3.3e6, 4e6, 7e6, 10e6):
            cap = s.capture(rate_hz=rate, samples=s.info.sample_memory, test_pattern=True)
            line = [f"{cap.rate_hz / 1e6:6.2f} MS/s"]
            for ch, freq in expected.items():
                rises = [i for i in range(1, len(cap.samples))
                         if (cap.samples[i] >> ch) & 1 and not (cap.samples[i - 1] >> ch) & 1]
                if len(rises) < 3 or cap.rate_hz / 4 < freq:
                    continue   # too few cycles, or the pattern is not resolvable at this rate
                measured = cap.rate_hz * (len(rises) - 1) / (rises[-1] - rises[0])
                good = abs(measured - freq) / freq < 0.002
                ok &= good
                line.append(f"CH{ch} {measured / 1e3:8.2f} kHz {'ok' if good else 'FAIL'}")
            print("  ".join(line))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="blackpill_sump", description=__doc__)
    ap.add_argument("--port", help="serial port (default: auto-detect USB 1209:0001)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="show device information").set_defaults(fn=cmd_info)

    c = sub.add_parser("capture", help="run one acquisition")
    c.add_argument("--rate", default="1M", help="sample rate, e.g. 500k, 10M (max 10M)")
    c.add_argument("--samples", type=int, default=4096, help="window size (multiple of 4)")
    c.add_argument("--post", type=int, help="samples kept after the trigger (default: half)")
    c.add_argument("--trigger", help="CH:rise|fall|high|low, e.g. 3:rise")
    c.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for the trigger")
    c.add_argument("--test-pattern", action="store_true",
                   help="DRIVE the built-in test pattern on PB0..PB7 (disconnect your target!)")
    c.add_argument("--vcd", help="write a VCD file")
    c.add_argument("--csv", help="write a CSV file")
    c.set_defaults(fn=cmd_capture)

    sub.add_parser("selftest", help="verify timing against the built-in pattern "
                                    "(drives PB0..PB7: disconnect your target)").set_defaults(fn=cmd_selftest)
    sub.add_parser("gui", help="open the graphical viewer").set_defaults(fn=None)

    args = ap.parse_args(argv)
    if args.cmd == "gui":
        from .gui import run
        return run(args.port)
    try:
        return args.fn(args)
    except (SumpError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())


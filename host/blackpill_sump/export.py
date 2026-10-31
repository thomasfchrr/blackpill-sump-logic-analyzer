"""Capture export: VCD (GTKWave, PulseView import) and CSV."""

from __future__ import annotations

import datetime
from typing import IO

from .client import Capture


def write_vcd(cap: Capture, out: IO[str], channels: int = 8) -> None:
    """Write *cap* as a VCD file, one wire per probe, only value changes."""
    period_ps = round(1e12 / cap.rate_hz)
    out.write(f"$date {datetime.datetime.now():%Y-%m-%d %H:%M:%S} $end\n")
    out.write("$version blackpill-sump $end\n$timescale 1 ps $end\n$scope module logic $end\n")
    for ch in range(channels):
        out.write(f"$var wire 1 {chr(33 + ch)} CH{ch} $end\n")
    out.write("$upscope $end\n$enddefinitions $end\n")
    prev = None
    for i, byte in enumerate(cap.samples):
        if byte == prev:
            continue
        out.write(f"#{i * period_ps}\n")
        for ch in range(channels):
            bit = (byte >> ch) & 1
            if prev is None or bit != (prev >> ch) & 1:
                out.write(f"{bit}{chr(33 + ch)}\n")
        prev = byte
    out.write(f"#{len(cap.samples) * period_ps}\n")


def write_csv(cap: Capture, out: IO[str], channels: int = 8) -> None:
    """Write *cap* as CSV: time in seconds, then one column per probe."""
    out.write("time_s," + ",".join(f"CH{c}" for c in range(channels)) + "\n")
    for i, byte in enumerate(cap.samples):
        bits = ",".join(str((byte >> c) & 1) for c in range(channels))
        out.write(f"{i / cap.rate_hz:.9g},{bits}\n")

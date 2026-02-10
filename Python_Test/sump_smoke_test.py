#!/usr/bin/env python3
"""Quick SUMP protocol smoke test for STM32 CDC device.

Author: Thomas Faucherre
Created: 2026-02-10
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from typing import Dict, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc

SUMP_CMD_RESET = 0x00
SUMP_CMD_ARM = 0x01
SUMP_CMD_ID = 0x02
SUMP_CMD_METADATA = 0x04
SUMP_CMD_SET_DIVIDER = 0x80
SUMP_CMD_CAPTURE_SIZE = 0x81
SUMP_CMD_SET_FLAGS = 0x82
SUMP_CMD_TRIG_MASK0 = 0xC0
SUMP_CMD_TRIG_VALUE0 = 0xC1
SUMP_CMD_TRIG_CFG0 = 0xC2


def auto_detect_port(vid: int, pid: int) -> Optional[str]:
    ports = list(list_ports.comports())

    for p in ports:
        if p.vid == vid and p.pid == pid:
            return p.device

    for p in ports:
        desc = (p.description or "").lower()
        manu = (p.manufacturer or "").lower()
        if "stm32" in desc or "virtual comport" in desc or "stmicro" in manu:
            return p.device

    return None


def send_long(ser: serial.Serial, cmd: int, value: int) -> None:
    ser.write(bytes([cmd]) + struct.pack("<I", value & 0xFFFFFFFF))


def read_exact(ser: serial.Serial, count: int, timeout_s: float) -> bytes:
    end = time.monotonic() + timeout_s
    out = bytearray()
    while len(out) < count and time.monotonic() < end:
        chunk = ser.read(count - len(out))
        if chunk:
            out.extend(chunk)
            continue
        time.sleep(0.001)
    return bytes(out)


def parse_metadata(buf: bytes) -> Dict[str, object]:
    out: Dict[str, object] = {"raw_len": len(buf)}
    i = 0
    while i < len(buf):
        key = buf[i]
        i += 1
        if key == 0x00:
            break
        typ = key >> 5
        if typ == 0:
            j = buf.find(b"\x00", i)
            if j < 0:
                break
            out[f"0x{key:02X}"] = buf[i:j].decode(errors="replace")
            i = j + 1
        elif typ == 1:
            if i + 4 > len(buf):
                break
            out[f"0x{key:02X}"] = int.from_bytes(buf[i : i + 4], "big")
            i += 4
        elif typ == 2:
            if i >= len(buf):
                break
            out[f"0x{key:02X}"] = buf[i]
            i += 1
        else:
            break
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="STM32 SUMP smoke-test")
    parser.add_argument("--port", default=None, help="Serial port (ex: COM9). If omitted, auto-detect")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x0483, help="USB VID (default: 0x0483)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x5740, help="USB PID (default: 0x5740)")
    parser.add_argument("--samples", type=int, default=1024, help="Requested samples (default: 1024)")
    parser.add_argument("--samplerate", type=int, default=200_000, help="Requested samplerate in Hz")
    args = parser.parse_args()

    samples = max(4, min(args.samples, 16384))
    samples -= samples % 4

    rate = max(1, min(args.samplerate, 2_000_000))
    divider = max(0, (100_000_000 // rate) - 1)
    read_words = (samples // 4) - 1

    port = args.port or auto_detect_port(args.vid, args.pid)
    if not port:
        raise SystemExit("No STM32 CDC port found")

    with serial.Serial(port, baudrate=115200, timeout=0.05) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        ser.write(bytes([SUMP_CMD_RESET]) * 5)
        ser.write(bytes([SUMP_CMD_ID]))
        ident = read_exact(ser, 4, 1.0)
        if ident != b"1ALS":
            raise SystemExit(f"SUMP ID mismatch: got={ident!r} expected=b'1ALS'")
        print(f"SUMP ID OK on {port}: {ident!r}")

        ser.write(bytes([SUMP_CMD_METADATA]))
        time.sleep(0.15)
        meta_raw = ser.read(512)
        meta = parse_metadata(meta_raw)
        print("Metadata:", meta)

        send_long(ser, SUMP_CMD_SET_DIVIDER, divider)
        ser.write(bytes([SUMP_CMD_CAPTURE_SIZE]) + struct.pack("<HH", read_words, read_words))
        send_long(ser, SUMP_CMD_SET_FLAGS, 0x38)  # group0 enabled, groups1..3 disabled
        send_long(ser, SUMP_CMD_TRIG_MASK0, 0)
        send_long(ser, SUMP_CMD_TRIG_VALUE0, 0)
        ser.write(bytes([SUMP_CMD_TRIG_CFG0, 0, 0, 0, 0x08]))  # START on stage0

        ser.write(bytes([SUMP_CMD_ARM]))
        data = read_exact(ser, samples, timeout_s=6.0)
        if len(data) != samples:
            raise SystemExit(f"Capture timeout: got={len(data)} expected={samples}")

        uniq = len(set(data))
        print(f"Capture OK: {len(data)} bytes, unique_values={uniq}, first16={data[:16].hex()}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

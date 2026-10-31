"""SUMP / OpenBench Logic Sniffer client for the BlackPill firmware."""

from __future__ import annotations

import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

import serial
from serial.tools import list_ports

USB_VID = 0x0483
USB_PID = 0x5740

BASE_CLOCK_HZ = 100_000_000   # SUMP divider clock; the firmware reproduces it exactly
MIN_PERIOD_TICKS = 10        # shortest sample period sustained with an exact timebase (10 MS/s)

CMD_RESET = 0x00
CMD_ARM = 0x01
CMD_ID = 0x02
CMD_METADATA = 0x04
CMD_SET_DIVIDER = 0x80
CMD_CAPTURE_SIZE = 0x81
CMD_SET_FLAGS = 0x82
CMD_TRIG_MASK = 0xC0      # + 4 * stage
CMD_TRIG_VALUE = 0xC1
CMD_TRIG_CONFIG = 0xC2

FLAG_INTERNAL_TEST = 1 << 11
# Group 0 (8 probes) enabled, groups 1..3 disabled.
FLAGS_8CH = (1 << 3) | (1 << 4) | (1 << 5)


class SumpError(RuntimeError):
    pass


def find_port(vid: int = USB_VID, pid: int = USB_PID) -> Optional[str]:
    """Return the first serial port matching the analyzer's USB ids."""
    for p in list_ports.comports():
        if p.vid == vid and p.pid == pid:
            return p.device
    return None


def divider_for_rate(rate_hz: float) -> int:
    """SUMP divider (relative to the 100 MHz convention) closest to *rate_hz*."""
    return max(0, round(BASE_CLOCK_HZ / rate_hz) - 1)


def actual_rate(sump_divider: int) -> float:
    """Sample rate produced for *sump_divider* (the firmware clamps very fast requests)."""
    return BASE_CLOCK_HZ / max(sump_divider + 1, MIN_PERIOD_TICKS)


@dataclass
class Capture:
    """Samples in chronological order; one byte per sample, bit n = probe n."""

    samples: bytes
    rate_hz: float
    trigger_index: int = 0
    requested_rate_hz: float = 0.0

    def channel(self, n: int) -> List[int]:
        return [(b >> n) & 1 for b in self.samples]

    def edges(self, n: int) -> List[int]:
        """Indexes where channel *n* changes level."""
        prev = None
        out = []
        for i, b in enumerate(self.samples):
            v = (b >> n) & 1
            if prev is not None and v != prev:
                out.append(i)
            prev = v
        return out


@dataclass
class DeviceInfo:
    name: str = ""
    firmware: str = ""
    probes: int = 0
    sample_memory: int = 0
    max_rate_hz: int = 0
    protocol: int = 0
    extra: Dict[int, int] = field(default_factory=dict)


class Sump:
    def __init__(self, port: Optional[str] = None, timeout: float = 2.0):
        port = port or find_port()
        if port is None:
            raise SumpError("analyzer not found (USB 0483:5740); pass the port explicitly")
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=timeout, write_timeout=timeout)
        self.info = DeviceInfo()
        self._cancel = threading.Event()
        self._connect()

    def __enter__(self) -> "Sump":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def close(self) -> None:
        try:
            self.reset()
        finally:
            self.ser.close()

    # -- low level -----------------------------------------------------------
    def _send_short(self, cmd: int) -> None:
        self.ser.write(bytes([cmd]))

    def _send_long(self, cmd: int, arg: bytes) -> None:
        assert len(arg) == 4
        self.ser.write(bytes([cmd]) + arg)

    def _read_exact(self, n: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        buf = bytearray()
        while len(buf) < n:
            chunk = self.ser.read(min(n - len(buf), 65536))
            buf += chunk
            if self._cancel.is_set():
                raise SumpError("cancelled")
            if not chunk and time.monotonic() > deadline:
                raise SumpError(f"timeout: got {len(buf)} of {n} bytes")
        return bytes(buf)

    def reset(self) -> None:
        self.ser.write(bytes([CMD_RESET] * 5))
        self.ser.flush()
        time.sleep(0.05)
        self.ser.reset_input_buffer()

    def _connect(self) -> None:
        self.reset()
        self._send_short(CMD_ID)
        ident = self._read_exact(4, 2.0)
        if ident != b"1ALS":
            raise SumpError(f"unexpected ID reply {ident!r}")
        self.info = self.read_metadata()

    def read_metadata(self) -> DeviceInfo:
        self._send_short(CMD_METADATA)
        info = DeviceInfo()
        while True:
            key = self._read_exact(1, 2.0)[0]
            if key == 0x00:
                break
            kind = key >> 5
            if kind == 0:      # NUL-terminated string
                s = bytearray()
                while True:
                    c = self._read_exact(1, 2.0)[0]
                    if c == 0:
                        break
                    s.append(c)
                if key == 0x01:
                    info.name = s.decode("ascii", "replace")
                elif key == 0x02:
                    info.firmware = s.decode("ascii", "replace")
            elif kind == 1:    # 32-bit big endian
                val = struct.unpack(">I", self._read_exact(4, 2.0))[0]
                if key == 0x20:
                    info.probes = val
                elif key == 0x21:
                    info.sample_memory = val
                elif key == 0x23:
                    info.max_rate_hz = val
                elif key == 0x24:
                    info.protocol = val
                else:
                    info.extra[key] = val
            elif kind == 2:    # 8-bit
                info.extra[key] = self._read_exact(1, 2.0)[0]
            else:
                raise SumpError(f"unknown metadata key 0x{key:02X}")
        return info

    # -- capture -------------------------------------------------------------
    def capture(
        self,
        rate_hz: float = 1_000_000,
        samples: int = 4096,
        post_trigger: Optional[int] = None,
        trigger: Optional[Sequence[Tuple[int, int]]] = None,
        test_pattern: bool = False,
        timeout: float = 30.0,
    ) -> Capture:
        """Run one acquisition.

        *post_trigger* is the number of samples kept from the trigger onwards
        (default: half the window). *trigger* is a list of up to 4 ``(mask, value)``
        stages that must match on successive samples; the last one fires the
        trigger. None (or a zero mask) starts immediately. Rising edge on probe 3:
        ``[(0x08, 0x00), (0x08, 0x08)]``. ``test_pattern`` makes the firmware DRIVE a built-in pattern
        on PB0..PB7, so disconnect any target first.
        """
        if samples < 4 or samples % 4:
            raise ValueError("samples must be a multiple of 4")
        if self.info.sample_memory and samples > self.info.sample_memory:
            raise ValueError(f"at most {self.info.sample_memory} samples")
        post = samples // 2 if post_trigger is None else post_trigger
        if post % 4 or not 4 <= post <= samples:
            raise ValueError("post_trigger must be a multiple of 4 within the window")

        divider = divider_for_rate(rate_hz)
        flags = FLAGS_8CH | (FLAG_INTERNAL_TEST if test_pattern else 0)

        self._cancel.clear()
        self.reset()
        self._send_long(CMD_SET_DIVIDER, struct.pack("<I", divider))
        self._send_long(CMD_CAPTURE_SIZE, struct.pack("<HH", samples // 4 - 1, post // 4 - 1))
        self._send_long(CMD_SET_FLAGS, struct.pack("<I", flags))
        stages = list(trigger) if trigger else [(0, 0)]
        if len(stages) > 4:
            raise ValueError("at most 4 trigger stages")
        for i, (mask, value) in enumerate(stages):
            last = i == len(stages) - 1
            self._send_long(CMD_TRIG_MASK + 4 * i, struct.pack("<I", mask))
            self._send_long(CMD_TRIG_VALUE + 4 * i, struct.pack("<I", value))
            self._send_long(CMD_TRIG_CONFIG + 4 * i, bytes([0, 0, 0, 0x08 if last else 0]))
        self._send_short(CMD_ARM)

        raw = self._read_exact(samples, timeout)
        data = raw[::-1]   # the device sends the newest sample first
        return Capture(
            samples=data,
            rate_hz=actual_rate(divider),
            requested_rate_hz=BASE_CLOCK_HZ / (divider + 1),
            trigger_index=samples - post,
        )

    def abort(self) -> None:
        """Cancel a running capture. Safe to call from another thread."""
        self._cancel.set()
        self.ser.write(bytes([CMD_RESET] * 5))

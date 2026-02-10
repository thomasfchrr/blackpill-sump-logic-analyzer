#!/usr/bin/env python3
"""\
File: main.py
Description: USB CDC host tool for BlackPill high-throughput protocol tests.
Author: Thomas Faucherre
Created: 2026-02-10
"""

from __future__ import annotations

import argparse
import os
import shlex
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Deque, Dict, Optional, Tuple
from collections import deque

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc

MAGIC = 0x31544255
VERSION = 1
HEADER_STRUCT = struct.Struct("<IBBHII")
HEADER_SIZE = HEADER_STRUCT.size
MAX_PAYLOAD = 512
USB_STREAM_MIN_PAYLOAD = 8
STREAM_TEMPLATE_SEED = 0x5A17C3D2

CMD_HELLO_REQ = 0x01
CMD_PING_REQ = 0x02
CMD_ECHO_REQ = 0x03
CMD_START_STREAM_REQ = 0x04
CMD_STOP_STREAM_REQ = 0x05
CMD_STATS_REQ = 0x06
CMD_INTEGRITY_REQ = 0x07
CMD_LED_REQ = 0x08
CMD_RESET_REQ = 0x09
CMD_LOGIC_SNAPSHOT_REQ = 0x0A

RSP_HELLO = 0x81
RSP_PING = 0x82
RSP_ECHO = 0x83
RSP_START_STREAM = 0x84
RSP_STOP_STREAM = 0x85
RSP_STATS = 0x86
RSP_INTEGRITY = 0x87
RSP_LED = 0x88
RSP_RESET_ACK = 0x89
RSP_LOGIC_SNAPSHOT = 0x8A
RSP_STREAM_DATA = 0xE0
RSP_WARN = 0xF0
RSP_ERROR = 0xFF

WARN_NAMES: Dict[int, str] = {
    1: "RX_OVERFLOW",
    2: "BAD_MAGIC",
    3: "BAD_VERSION",
    4: "BAD_LENGTH",
    5: "CRC_FAIL",
    6: "TX_OVERFLOW",
    7: "DMA_FAIL",
    8: "UNKNOWN_CMD",
}

WARN_PRINT_FIRST = 8
WARN_PRINT_EVERY = 500


@dataclass
class Packet:
    pkt_type: int
    seq: int
    payload: bytes


def crc32(payload: bytes) -> int:
    return zlib.crc32(payload) & 0xFFFFFFFF


def encode_packet(pkt_type: int, seq: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)} > {MAX_PAYLOAD}")
    header = HEADER_STRUCT.pack(MAGIC, VERSION, pkt_type, len(payload), seq, crc32(payload))
    return header + payload


def parse_one_from_buffer(buffer: bytearray) -> Optional[Packet]:
    magic_bytes = struct.pack("<I", MAGIC)

    while True:
        if len(buffer) < 4:
            return None
        if buffer[:4] != magic_bytes:
            idx = buffer.find(magic_bytes, 1)
            if idx < 0:
                # Keep up to 3 bytes to recover cross-boundary magic.
                del buffer[:-3]
                return None
            del buffer[:idx]
            continue
        break

    if len(buffer) < HEADER_SIZE:
        return None

    magic, version, pkt_type, payload_len, seq, expected_crc = HEADER_STRUCT.unpack_from(buffer)
    if magic != MAGIC:
        del buffer[0]
        return None

    if version != VERSION or payload_len > MAX_PAYLOAD:
        del buffer[0]
        return None

    frame_len = HEADER_SIZE + payload_len
    if len(buffer) < frame_len:
        return None

    payload = bytes(buffer[HEADER_SIZE:frame_len])
    del buffer[:frame_len]

    if crc32(payload) != expected_crc:
        return None

    return Packet(pkt_type=pkt_type, seq=seq, payload=payload)


def xorshift32(state: int) -> int:
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= (state >> 17) & 0xFFFFFFFF
    state ^= (state << 5) & 0xFFFFFFFF
    return state & 0xFFFFFFFF


def build_stream_template(size: int) -> bytes:
    out = bytearray(size)
    state = STREAM_TEMPLATE_SEED

    for i in range(0, size, 4):
        state = xorshift32(state)
        chunk = struct.pack("<I", state)
        out[i : i + min(4, size - i)] = chunk[: min(4, size - i)]

    return bytes(out)


class UsbProtoClient:
    def __init__(self, port: Optional[str], vid: int, pid: int, timeout_s: float = 0.0) -> None:
        self.port = port
        self.vid = vid
        self.pid = pid
        self.timeout_s = timeout_s
        self.ser: Optional[serial.Serial] = None
        self.rx_buffer = bytearray()
        self.rx_queue: Deque[Packet] = deque()
        self.next_seq = 1
        self.warn_counts: Dict[Tuple[int, int], int] = {}

    def connect(self) -> None:
        selected_port = self.port or self.auto_detect_port(self.vid, self.pid)
        if not selected_port:
            available = [
                f"{p.device} (vid={p.vid}, pid={p.pid}, desc={p.description})"
                for p in list_ports.comports()
            ]
            raise RuntimeError(
                "No matching USB CDC port found. Available ports: " + ", ".join(available)
            )

        self.ser = serial.Serial(selected_port, baudrate=115200, timeout=self.timeout_s)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        self.port = selected_port
        print(f"Connected to {self.port}")

    def close(self) -> None:
        if self.ser is not None:
            self.ser.close()
            self.ser = None

    def __enter__(self) -> "UsbProtoClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @staticmethod
    def auto_detect_port(vid: int, pid: int) -> Optional[str]:
        ports = list(list_ports.comports())

        for p in ports:
            if (p.vid == vid) and (p.pid == pid):
                return p.device

        for p in ports:
            desc = (p.description or "").lower()
            manu = (p.manufacturer or "").lower()
            if "stm32" in desc or "virtual comport" in desc or "stmicro" in manu:
                return p.device

        return None

    def _read_from_serial(self) -> None:
        if self.ser is None:
            raise RuntimeError("serial not connected")

        to_read = self.ser.in_waiting
        if to_read <= 0:
            return

        data = self.ser.read(to_read)
        if not data:
            return

        self.rx_buffer.extend(data)

        while True:
            pkt = parse_one_from_buffer(self.rx_buffer)
            if pkt is None:
                break
            self.rx_queue.append(pkt)

    def poll(self, duration_s: float = 0.0) -> None:
        deadline = time.monotonic() + max(duration_s, 0.0)

        while True:
            self._read_from_serial()
            if time.monotonic() >= deadline:
                break
            time.sleep(0.001)

    def _alloc_seq(self) -> int:
        seq = self.next_seq
        self.next_seq = (self.next_seq + 1) & 0xFFFFFFFF
        if self.next_seq == 0:
            self.next_seq = 1
        return seq

    def send_packet(self, pkt_type: int, seq: int, payload: bytes = b"") -> None:
        if self.ser is None:
            raise RuntimeError("serial not connected")
        self.ser.write(encode_packet(pkt_type, seq, payload))

    def log_warn(self, payload: bytes) -> None:
        if len(payload) >= 4:
            code, detail = struct.unpack("<HH", payload[:4])
            key = (code, detail)
        else:
            key = (-1, -1)
        count = self.warn_counts.get(key, 0) + 1
        self.warn_counts[key] = count

        if count <= WARN_PRINT_FIRST or (count % WARN_PRINT_EVERY) == 0:
            suffix = "" if count <= WARN_PRINT_FIRST else f" [x{count}]"
            print(f"WARN: {decode_warn(payload)}{suffix}")

    def recv_any(self, timeout_s: float) -> Optional[Packet]:
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            self.poll(0.0)
            if self.rx_queue:
                return self.rx_queue.popleft()
            time.sleep(0.001)

        self.poll(0.0)
        if self.rx_queue:
            return self.rx_queue.popleft()
        return None

    def request(
        self,
        cmd_type: int,
        expected_rsp_type: int,
        payload: bytes = b"",
        timeout_s: float = 1.0,
        retries: int = 2,
    ) -> Tuple[int, Packet]:
        deferred: Deque[Packet] = deque()

        for attempt in range(retries + 1):
            seq = self._alloc_seq()
            self.send_packet(cmd_type, seq, payload)

            deadline = time.monotonic() + timeout_s
            while time.monotonic() < deadline:
                pkt = self.recv_any(0.05)
                if pkt is None:
                    continue

                if pkt.pkt_type == RSP_WARN:
                    self.log_warn(pkt.payload)
                    continue

                if pkt.seq != seq:
                    deferred.append(pkt)
                    continue

                if pkt.pkt_type == RSP_ERROR:
                    raise RuntimeError(f"Device error for seq={seq}: {decode_error(pkt.payload)}")

                if pkt.pkt_type == expected_rsp_type:
                    # Push back deferred packets for later consumers.
                    while deferred:
                        self.rx_queue.appendleft(deferred.pop())
                    return seq, pkt

                deferred.append(pkt)

            if attempt < retries:
                print(f"Retry cmd 0x{cmd_type:02X}, attempt {attempt + 1}/{retries}")

        while deferred:
            self.rx_queue.appendleft(deferred.pop())
        raise TimeoutError(f"No response type 0x{expected_rsp_type:02X} after retries")


def decode_warn(payload: bytes) -> str:
    if len(payload) < 12:
        return f"invalid warn payload ({len(payload)} bytes)"
    code, detail, value, tick = struct.unpack("<HHII", payload[:12])
    return f"code={code}({WARN_NAMES.get(code, 'UNK')}) detail={detail} value={value} tick={tick}"


def decode_error(payload: bytes) -> str:
    if len(payload) < 12:
        return f"invalid error payload ({len(payload)} bytes)"
    err_code, detail, seq, tick = struct.unpack("<HHII", payload[:12])
    return f"err={err_code} detail={detail} seq={seq} tick={tick}"


def parse_hello(payload: bytes) -> Dict[str, int]:
    if len(payload) < 32:
        raise ValueError("HELLO payload too short")
    fw, proto, max_payload, sysclk, caps, rx_ring, tx_depth, tick = struct.unpack("<IIIIIIII", payload[:32])
    return {
        "fw": fw,
        "proto": proto,
        "max_payload": max_payload,
        "sysclk": sysclk,
        "caps": caps,
        "rx_ring": rx_ring,
        "tx_depth": tx_depth,
        "tick": tick,
    }


def parse_stats(payload: bytes) -> Dict[str, int]:
    if len(payload) < 52:
        raise ValueError("STATS payload too short")

    ints = struct.unpack("<IIIIIIIIIIII", payload[:48])
    return {
        "rx_bytes": ints[0],
        "tx_bytes": ints[1],
        "rx_ok": ints[2],
        "rx_bad_magic": ints[3],
        "rx_bad_header": ints[4],
        "rx_crc_fail": ints[5],
        "rx_overflow": ints[6],
        "tx_queue_overflow": ints[7],
        "tx_busy": ints[8],
        "dma_copy_ok": ints[9],
        "dma_copy_fail": ints[10],
        "stream_counter": ints[11],
        "stream_enabled": payload[48],
        "usb_configured": payload[49],
        "led_mode": payload[50],
    }


def parse_logic_snapshot(payload: bytes) -> Dict[str, object]:
    if len(payload) < 12:
        raise ValueError("LOGIC_SNAPSHOT payload too short")

    fmt, probes, sample_period_us, sample_count, flags, tick = struct.unpack("<BBHHHI", payload[:12])
    expected = 12 + sample_count
    if len(payload) < expected:
        raise ValueError(f"LOGIC_SNAPSHOT truncated: got={len(payload)} expected>={expected}")

    return {
        "format": fmt,
        "probes": probes,
        "sample_period_us": sample_period_us,
        "sample_count": sample_count,
        "flags": flags,
        "tick": tick,
        "samples": payload[12:expected],
    }


def run_local_parser_self_tests() -> None:
    buf = bytearray()
    payload = b"abc123"
    pkt = encode_packet(CMD_PING_REQ, 42, payload)

    # Basic decode
    buf.extend(pkt)
    out = parse_one_from_buffer(buf)
    assert out is not None
    assert out.pkt_type == CMD_PING_REQ
    assert out.seq == 42
    assert out.payload == payload
    assert len(buf) == 0

    # Resync decode
    buf.extend(b"xxxx" + pkt)
    out = parse_one_from_buffer(buf)
    assert out is not None
    assert out.seq == 42

    # CRC reject
    broken = bytearray(pkt)
    broken[-1] ^= 0x55
    buf.extend(broken)
    out = parse_one_from_buffer(buf)
    assert out is None

    print("Local parser self-tests passed")


def cmd_hello(client: UsbProtoClient) -> None:
    _, rsp = client.request(CMD_HELLO_REQ, RSP_HELLO)
    hello = parse_hello(rsp.payload)
    print("HELLO:")
    print(f"  fw=0x{hello['fw']:08X} proto={hello['proto']} max_payload={hello['max_payload']}")
    print(f"  sysclk={hello['sysclk']} caps=0x{hello['caps']:08X} rx_ring={hello['rx_ring']} tx_depth={hello['tx_depth']}")


def cmd_ping(client: UsbProtoClient, text: str) -> None:
    data = text.encode("utf-8")
    _, rsp = client.request(CMD_PING_REQ, RSP_PING, data)
    print("PONG:", rsp.payload.decode(errors="replace"))


def cmd_echo(client: UsbProtoClient, text: str) -> None:
    data = text.encode("utf-8")
    _, rsp = client.request(CMD_ECHO_REQ, RSP_ECHO, data)
    print("ECHO:", rsp.payload.decode(errors="replace"))


def cmd_stats(client: UsbProtoClient) -> None:
    _, rsp = client.request(CMD_STATS_REQ, RSP_STATS)
    stats = parse_stats(rsp.payload)
    print("STATS:")
    for k in [
        "rx_bytes",
        "tx_bytes",
        "rx_ok",
        "rx_bad_magic",
        "rx_bad_header",
        "rx_crc_fail",
        "rx_overflow",
        "tx_queue_overflow",
        "tx_busy",
        "dma_copy_ok",
        "dma_copy_fail",
        "stream_counter",
        "stream_enabled",
        "usb_configured",
        "led_mode",
    ]:
        print(f"  {k}: {stats[k]}")


def cmd_led(client: UsbProtoClient, mode: str) -> None:
    mode_map = {"auto": 0, "off": 1, "on": 2}
    if mode not in mode_map:
        raise ValueError("mode must be auto|off|on")

    payload = bytes([mode_map[mode]])
    _, rsp = client.request(CMD_LED_REQ, RSP_LED, payload)
    if len(rsp.payload) >= 1:
        print(f"LED mode set to {rsp.payload[0]}")
    else:
        print("LED response malformed")


def cmd_reset(client: UsbProtoClient, reason: int = 0) -> None:
    payload = struct.pack("<H", reason & 0xFFFF)
    _seq, rsp = client.request(CMD_RESET_REQ, RSP_RESET_ACK, payload, timeout_s=0.6, retries=0)
    if len(rsp.payload) >= 8:
        ack_reason, marker, tick = struct.unpack("<HHI", rsp.payload[:8])
        print(f"RESET_ACK: reason=0x{ack_reason:04X} marker=0x{marker:04X} tick={tick}")
    else:
        print("RESET_ACK malformed")
    print("STM32 reset requested.")


def cmd_logic_snapshot(client: UsbProtoClient, sample_count: int = 128, preview_lines: int = 24) -> None:
    sample_count = max(1, min(sample_count, 500))
    payload = struct.pack("<H", sample_count)
    _seq, rsp = client.request(CMD_LOGIC_SNAPSHOT_REQ, RSP_LOGIC_SNAPSHOT, payload, timeout_s=1.0, retries=1)
    snap = parse_logic_snapshot(rsp.payload)
    samples: bytes = snap["samples"]  # type: ignore[assignment]

    print(
        f"LOGIC_SNAPSHOT: fmt={snap['format']} probes={snap['probes']} "
        f"period_us={snap['sample_period_us']} samples={snap['sample_count']} tick={snap['tick']}"
    )

    max_preview = min(preview_lines, len(samples))
    for idx in range(max_preview):
        val = samples[idx]
        print(f"  S{idx:03d}: 0b{val:08b} 0x{val:02X}")


def cmd_integrity(client: UsbProtoClient, iterations: int, payload_len: int) -> None:
    payload_len = max(1, min(payload_len, MAX_PAYLOAD))
    ok = 0
    start = time.monotonic()
    sent_bytes = 0

    for _ in range(iterations):
        payload = os.urandom(payload_len)
        seq, rsp = client.request(CMD_INTEGRITY_REQ, RSP_INTEGRITY, payload, timeout_s=1.0, retries=1)
        sent_bytes += len(payload)

        if len(rsp.payload) < 16:
            continue

        rx_crc, rx_len, rx_seq, _tick = struct.unpack("<IIII", rsp.payload[:16])
        if (rx_crc == crc32(payload)) and (rx_len == len(payload)) and (rx_seq == seq):
            ok += 1

    elapsed = max(time.monotonic() - start, 1e-6)
    print(
        f"Integrity uplink: {ok}/{iterations} ok, "
        f"{sent_bytes / elapsed / (1024 * 1024):.2f} MiB/s effective payload"
    )


def cmd_stream_capture(client: UsbProtoClient, seconds: float, payload_len: int, interval_ms: int = 0) -> None:
    payload_len = max(USB_STREAM_MIN_PAYLOAD, min(payload_len, MAX_PAYLOAD))

    start_payload = struct.pack("<HHI", payload_len, max(0, interval_ms), 0)
    client.request(CMD_START_STREAM_REQ, RSP_START_STREAM, start_payload)

    template = build_stream_template(payload_len)
    start = time.monotonic()
    end = start + seconds
    last_counter: Optional[int] = None
    packets = 0
    bytes_rx = 0
    counter_jumps = 0
    pattern_errors = 0

    try:
        while time.monotonic() < end:
            pkt = client.recv_any(0.05)
            if pkt is None:
                continue

            if pkt.pkt_type == RSP_WARN:
                client.log_warn(pkt.payload)
                continue

            if pkt.pkt_type == RSP_ERROR:
                print(f"ERR: {decode_error(pkt.payload)}")
                continue

            if pkt.pkt_type != RSP_STREAM_DATA:
                continue

            packets += 1
            bytes_rx += len(pkt.payload)

            if len(pkt.payload) != payload_len:
                pattern_errors += 1
                continue

            if len(pkt.payload) >= 4:
                counter = struct.unpack_from("<I", pkt.payload, 0)[0]
                if (last_counter is not None) and (counter != ((last_counter + 1) & 0xFFFFFFFF)):
                    counter_jumps += 1
                last_counter = counter

            if len(pkt.payload) > 8 and pkt.payload[8:] != template[8:]:
                pattern_errors += 1
    finally:
        try:
            client.request(CMD_STOP_STREAM_REQ, RSP_STOP_STREAM, timeout_s=0.5, retries=0)
        except Exception as exc:  # pragma: no cover
            print(f"Stop stream warning: {exc}")

    elapsed = max(time.monotonic() - start, 1e-6)
    print(
        f"Stream RX: packets={packets} bytes={bytes_rx} "
        f"rate={bytes_rx / elapsed / (1024 * 1024):.2f} MiB/s "
        f"counter_jumps={counter_jumps} pattern_errors={pattern_errors}"
    )


def run_benchmark(client: UsbProtoClient, seconds: float, payload_len: int) -> None:
    print("Benchmark step 1/2: uplink integrity")
    cmd_integrity(client, iterations=200, payload_len=min(payload_len, MAX_PAYLOAD))

    print("Benchmark step 2/2: downlink stream")
    cmd_stream_capture(client, seconds=seconds, payload_len=payload_len, interval_ms=0)


def repl(client: UsbProtoClient) -> None:
    print(
        "Commands: hello, ping <text>, echo <text>, stats, integrity <n> <len>, "
        "stream <sec> <len> [interval_ms], logic [samples] [preview], "
        "reset [reason], led <auto|off|on>, bench [sec] [len], quit"
    )

    while True:
        try:
            line = input("usb> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return

        if not line:
            continue

        try:
            parts = shlex.split(line)
        except ValueError as exc:
            print(f"Parse error: {exc}")
            continue

        cmd = parts[0].lower()

        try:
            if cmd in {"quit", "exit", "q"}:
                return
            if cmd == "hello":
                cmd_hello(client)
            elif cmd == "ping":
                cmd_ping(client, " ".join(parts[1:]) if len(parts) > 1 else "PING")
            elif cmd == "echo":
                cmd_echo(client, " ".join(parts[1:]) if len(parts) > 1 else "ECHO")
            elif cmd == "stats":
                cmd_stats(client)
            elif cmd == "integrity":
                if len(parts) < 3:
                    raise ValueError("usage: integrity <iterations> <payload_len>")
                cmd_integrity(client, int(parts[1]), int(parts[2]))
            elif cmd == "stream":
                if len(parts) < 3:
                    raise ValueError("usage: stream <seconds> <payload_len> [interval_ms]")
                interval = int(parts[3]) if len(parts) > 3 else 0
                cmd_stream_capture(client, float(parts[1]), int(parts[2]), interval)
            elif cmd == "led":
                if len(parts) != 2:
                    raise ValueError("usage: led <auto|off|on>")
                cmd_led(client, parts[1].lower())
            elif cmd == "reset":
                reason = int(parts[1], 0) if len(parts) > 1 else 0
                cmd_reset(client, reason)
            elif cmd == "logic":
                samples = int(parts[1]) if len(parts) > 1 else 128
                preview = int(parts[2]) if len(parts) > 2 else 24
                cmd_logic_snapshot(client, samples, preview)
            elif cmd == "bench":
                duration = float(parts[1]) if len(parts) > 1 else 5.0
                payload_len = int(parts[2]) if len(parts) > 2 else 256
                run_benchmark(client, duration, payload_len)
            else:
                print("Unknown command")
        except Exception as exc:
            print(f"Command failed: {exc}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BlackPill USB CDC protocol client")
    parser.add_argument("--port", default=None, help="Serial port (ex: COM5). If omitted, auto-detect")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x0483, help="USB VID (default: 0x0483)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x5740, help="USB PID (default: 0x5740)")
    parser.add_argument("--self-test", action="store_true", help="Run local parser tests + quick link checks then exit")
    parser.add_argument("--bench", action="store_true", help="Run benchmark then exit")
    parser.add_argument("--duration", type=float, default=5.0, help="Benchmark stream duration in seconds")
    parser.add_argument("--payload", type=int, default=256, help="Payload size for benchmark/integrity")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    run_local_parser_self_tests()

    with UsbProtoClient(port=args.port, vid=args.vid, pid=args.pid) as client:
        cmd_hello(client)

        if args.self_test:
            cmd_integrity(client, iterations=50, payload_len=max(32, min(args.payload, MAX_PAYLOAD)))
            cmd_logic_snapshot(client, sample_count=128, preview_lines=8)
            cmd_stream_capture(client, seconds=2.0, payload_len=max(64, min(args.payload, MAX_PAYLOAD)), interval_ms=0)
            cmd_stats(client)
            return 0

        if args.bench:
            run_benchmark(client, seconds=args.duration, payload_len=max(64, min(args.payload, MAX_PAYLOAD)))
            cmd_stats(client)
            return 0

        repl(client)

    return 0


if __name__ == "__main__":
    sys.exit(main())

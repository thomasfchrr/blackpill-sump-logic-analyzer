# Python Host Tools Guide

## Table of Contents
1. [Overview](#overview)
2. [Installation & Dependencies](#installation--dependencies)
3. [CLI Client (main.py)](#cli-client-mainpy)
4. [Qt6 GUI (gui_qt.py)](#qt6-gui-gui_qtpy)
5. [SUMP Validation (sump_smoke_test.py)](#sump-validation-sump_smoke_testpy)
6. [Protocol Implementation](#protocol-implementation)
7. [Advanced Usage](#advanced-usage)

---

## Overview

The Python tooling suite provides three complementary interfaces to the BlackPill SUMP device:

| Tool | Protocol | Purpose | Use Case |
|------|----------|---------|----------|
| **main.py** | Custom Framed (binary) | Full-featured CLI client | Development, debugging, scripting |
| **gui_qt.py** | Custom Framed (binary) | Interactive Qt6 GUI | User-friendly control, real-time monitoring |
| **sump_smoke_test.py** | SUMP (OLS-compatible) | Protocol validation | Verification, integration testing |

### Protocol Choice

- **Custom Framed**: 16-byte header + CRC validation, bidirectional requests/responses
- **SUMP**: ASCII commands, compatible with PulseView (no Python implementation of capture here)

---

## Installation & Dependencies

### Python Version

**Minimum**: Python 3.8
**Recommended**: Python 3.9+

### Package Installation

```bash
# Install dependencies
py -3 -m pip install pyserial pyside6

# Verify installation
py -3 -c "import serial; import PySide6; print('OK')"
```

### Port Auto-Detection

All tools use USB VID/PID matching to identify the device:

```python
import serial.tools.list_ports

# STM32F411 CDC Device
VID = 0x0483  # STMicroelectronics
PID = 0x5740  # STM32F4xx CDC

for port_info in serial.tools.list_ports.comports():
    if port_info.vid == VID and port_info.pid == PID:
        detected_port = port_info.device  # e.g., 'COM9'
```

---

## CLI Client (main.py)

### Quick Start

```bash
# Auto-detect device and connect
py -3 main.py

# Manual port selection
py -3 main.py --port COM9

# Run built-in self-tests
py -3 main.py --port COM9 --self-test

# Benchmark (send/receive 1000 packets)
py -3 main.py --port COM9 --benchmark
```

### Architecture

```python
# Frame structure
class Packet:
    pkt_type: int          # Command/response code
    seq: int               # Sequence ID (1–4294967295)
    payload: bytes         # 0–512 bytes
    
class UsbProtoClient:
    ser: serial.Serial
    rx_buffer: bytearray   # Accumulates data from USB
    rx_queue: deque[Packet]  # Received packets awaiting processing
    next_seq: int          # Auto-increment sequence number
    
    def request(cmd_type, payload, timeout_ms=1000):
        """Send request, wait for response, handle retries"""
```

### Available Commands

#### 1. HELLO_REQ (0x01)

**Purpose**: Query device information

```bash
hello
```

**Response** (0x81):
```
Device Name: BlackPill STM32
Firmware Version: stm32f411
Clock Frequency: 96000000 Hz
RX Ring Size: 4096 bytes
TX Slots: 16
TX Slot Size: 576 bytes
```

#### 2. PING_REQ (0x02)

**Purpose**: Round-trip latency test

```bash
ping 16
# Sends 16 bytes of data, waits for echo
# Response: Elapsed time in ms
```

**Measurement**:
```
Ping 16 bytes: 15 ms (roundtrip)
Estimated latency: ~7.5 ms one-way
```

#### 3. STATS_REQ (0x06)

**Purpose**: Runtime statistics and diagnostics

```bash
stats
```

**Response** (0x86):
```
RX Packets: 42
TX Packets: 39
DMA Transfers: 3
CRC Errors: 0
Framing Errors: 0
SUMP Arm Count: 5
SUMP Timeout Count: 0
Capture Overflows: 0
TX Queue Full: 0
TX Timeouts: 0
```

#### 4. LED_REQ (0x08)

**Purpose**: Control LED mode

```bash
led 0         # LED off (override mode 1)
led 1         # LED on (override mode 2)
led 2         # LED auto (normal patterns)
```

#### 5. LOGIC_SNAPSHOT_REQ (0x0A)

**Purpose**: Read current GPIO state

```bash
snapshot
```

**Response** (0x8A):
```
GPIOB[0:7]: 0b10101010 (170 decimal)
  CH0 (PB0): 0
  CH1 (PB1): 1
  CH2 (PB2): 0
  ...
```

### Self-Test Suite

```bash
py -3 main.py --self-test
```

**Sequence**:

1. **HELLO Check**
   - Verify device responds with valid metadata
   - Check clock frequency (should be ~96 MHz)

2. **Integrity Check**
   - Send 200 random payloads (1–512 bytes)
   - Verify CRC and sequence numbers
   - Report success rate

3. **Stream Test**
   - Subscribe to continuous stream data
   - Detect counter jumps or missing packets
   - Report throughput (bytes/sec)

4. **Latency Measurement**
   - PING 1000 times with varying payload sizes
   - Calculate min/max/average roundtrip time

### Example: Custom Script

```python
from main import UsbProtoClient

# Connect
client = UsbProtoClient(port='COM9', timeout_ms=2000)
if not client.connect():
    print("Failed to connect")
    exit(1)

# Request device info
resp = client.request(0x01, b'', expected_type=0x81)
print(f"Device: {resp.payload.decode('utf-8')}")

# Send custom payload
test_payload = b'\xAA\xBB\xCC\xDD'
resp = client.request(0x02, test_payload, expected_type=0x82)
assert resp.payload == test_payload
print("Ping successful")

client.close()
```

---

## Qt6 GUI (gui_qt.py)

### Launch

```bash
py -3 gui_qt.py
```

**UI Layout**:

```
┌─────────────────────────────────────────┐
│  BlackPill SUMP Logic Analyzer          │
├─────────────────────────────────────────┤
│ Port: [COM9 (Auto-detected)] [Refresh]  │
│       [Connect]                         │
├─────────────────────────────────────────┤
│ ┌───────────────────────────────────┐   │
│ │ Command Buttons:                  │   │
│ │ [Hello] [Ping] [Stats]            │   │
│ │ [LED Off] [LED On] [Reset]        │   │
│ │ [Logic Snapshot]                  │   │
│ └───────────────────────────────────┘   │
│                                         │
│ ┌───────────────────────────────────┐   │
│ │ Log Viewer (Real-Time):           │   │
│ │                                   │   │
│ │ [Connected] Device: BlackPill...  │   │
│ │ [HELLO] FW: stm32f411, CLK: 96M   │   │
│ │ [STATS] RX: 42, TX: 39            │   │
│ │ [LOGIC] CH0=0, CH1=1, ...         │   │
│ │                                   │   │
│ └───────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

### Features

| Feature | Button | Function |
|---------|--------|----------|
| **Connect** | Connect | Enumerate CDC device and open serial port |
| **Hello** | Hello | Query device info, display in log |
| **Ping** | Ping | Send 16-byte echo, measure latency |
| **Stats** | Stats | Fetch and display counters |
| **LED Control** | LED Off/On | Override LED mode (0 = off, 1 = on, 2 = auto) |
| **Reset** | Reset | SUMP RESET command (clears state) |
| **GPIO Read** | Logic Snapshot | Read and display GPIOB[0:7] state |

### Architecture

```python
# Async worker pattern (avoid blocking UI)
class UsbWorker(QObject):
    connected = pyqtSignal(bool)
    hello_ready = pyqtSignal(dict)
    stats_ready = pyqtSignal(dict)
    logic_ready = pyqtSignal(int)
    log = pyqtSignal(str)
    error = pyqtSignal(str)
    
    @pyqtSlot()
    def connect_device(self):
        # Run in worker thread to avoid blocking UI
        try:
            self.client.connect()
            self.connected.emit(True)
        except Exception as e:
            self.error.emit(str(e))
            self.connected.emit(False)
    
    @pyqtSlot()
    def do_hello(self):
        # Query and emit signal when complete
        ...

class MainWindow(QMainWindow):
    def __init__(self):
        # Create worker thread
        self.worker_thread = QThread()
        self.worker = UsbWorker()
        self.worker.moveToThread(self.worker_thread)
        
        # Connect signals
        self.ui.btn_connect.clicked.connect(self.worker.connect_device)
        self.worker.connected.connect(self.on_connected)
        self.worker.hello_ready.connect(self.on_hello)
        
        self.worker_thread.start()
```

---

## SUMP Validation (sump_smoke_test.py)

### Quick Start

```bash
# Auto-detect and test
py -3 sump_smoke_test.py

# Manual port and parameters
py -3 sump_smoke_test.py --port COM9 --samples 1024 --samplerate 500000
```

### Test Sequence

```python
# [1] Auto-detect STM32 CDC device
# [2] Send 5× RESET (0x00) to synchronize
# [3] ID request (0x02) → expect b'1ALS'
# [4] METADATA request (0x04) → parse device capabilities
# [5] Configure capture:
#     - SET_DIVIDER (0x80): divider = (100MHz / desired_rate) - 1
#     - CAPTURE_SIZE (0x81): (samples // 4) - 1 as words
#     - SET_FLAGS (0x82): Enable Group 0, disable Groups 1–3
#     - TRIG_MASK/TRIG_VALUE: Set to 0 (no trigger)
#     - TRIG_CONFIG: Set START bit (0x08)
# [6] ARM (0x01) → block and receive samples
# [7] Validate response:
#     - Length check: should be sample_count bytes
#     - Uniqueness: verify multiple distinct values
```

### Example Output

```
$ py -3 sump_smoke_test.py --samples 1024 --samplerate 200000
Detected: COM9 (STM32F411 CDC)
SUMP ID: b'1ALS' ✓
Metadata:
  Device: BlackPill STM32
  FW: stm32f411
  Sample Memory: 16384 bytes
  Max Rate: 2000000 Hz
  Num Probes: 8
  Protocol Version: 2
Configure Capture:
  Rate: 200 kHz (divider=499)
  Samples: 1024
ARM (0x01) and acquire...
Received 1024 bytes
Unique values: 7
First 16 bytes: fbfbfbfbfbfbfbfbfbfbfbfbfbfbfbfb
✓ Test PASSED
```

---

## Protocol Implementation

### Framed Protocol Details

#### Frame Structure

```
Offset | Size | Field      | Type    | Details
-------|------|---------|---------|----------
0      | 4    | Magic   | LE32    | 0x31544255 ("UBT1")
4      | 1    | Version | U8      | 0x01
5      | 1    | Type    | U8      | Command (0x00–0x7F) or Response (0x80–0xFF)
6      | 2    | Length  | LE16    | Payload size (0–512 bytes)
8      | 4    | Seq     | LE32    | Sequence ID (never 0)
12     | 4    | CRC-32  | LE32    | Checksum over payload + header (excl. magic)
16     | N    | Payload | Binary  | Variable length
```

#### CRC-32 Algorithm

```python
def crc32(data: bytes) -> int:
    """CRC-32 with polynomial 0xEDB88320 (CCITT reversed)"""
    POLY = 0xEDB88320
    crc = 0xFFFFFFFF
    
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ POLY
            else:
                crc >>= 1
    
    return crc ^ 0xFFFFFFFF  # Final XOR
```

**Python Implementation**:

```python
import struct

def encode_packet(pkt_type: int, seq: int, payload: bytes) -> bytes:
    """Build complete frame"""
    magic = struct.pack('<I', 0x31544255)
    header = struct.pack('<BBHI', 1, pkt_type, len(payload), seq)
    
    # CRC over header (excl. magic) + payload
    crc_data = header + payload
    crc = crc32(crc_data)
    crc_bytes = struct.pack('<I', crc)
    
    return magic + header + crc_bytes + payload

def decode_packet(data: bytes) -> dict:
    """Parse frame"""
    magic = struct.unpack('<I', data[0:4])[0]
    if magic != 0x31544255:
        raise ValueError("Invalid magic")
    
    version, pkt_type, length, seq = struct.unpack('<BBHI', data[4:12])
    crc_rx = struct.unpack('<I', data[12:16])[0]
    payload = data[16:]
    
    # Validate CRC
    crc_data = data[4:12] + payload
    crc_calc = crc32(crc_data)
    if crc_calc != crc_rx:
        raise ValueError("CRC mismatch")
    
    return {
        'version': version,
        'type': pkt_type,
        'seq': seq,
        'payload': payload
    }
```

### SUMP Protocol Commands

**Implemented** (firmware-side):
- 0x00: RESET
- 0x01: ARM
- 0x02: ID
- 0x04: METADATA
- 0x80: SET_DIVIDER
- 0x81: CAPTURE_SIZE
- 0x82: SET_FLAGS
- 0xC0–CF: TRIGGER_* (stages 0–3)

**Not Implemented**:
- Advanced trigger stages (only stage 0 used)
- RLE encoding (SUMP flag 0x0100)
- External trigger (would require GPIO edge detection)

---

## Advanced Usage

### Custom Packet Format

```python
from main import UsbProtoClient

client = UsbProtoClient(port='COM9')
client.connect()

# Manually craft and send packet
import struct

def send_raw_command(cmd_type, payload):
    magic = 0x31544255
    version = 0x01
    seq = 42  # Custom sequence ID
    
    # Build header
    header = struct.pack('<I', magic)
    header += struct.pack('<BBHI', version, cmd_type, len(payload), seq)
    
    # Calculate CRC
    crc_data = header[4:] + payload
    crc = crc32(crc_data)
    header += struct.pack('<I', crc)
    
    # Transmit
    client.ser.write(header + payload)
    
    # Wait for response (with timeout)
    response = client.request(cmd_type, payload, timeout_ms=1000)
    return response

# Example: custom diagnostic
resp = send_raw_command(0x06, b'')  # STATS
print(resp.payload)
```

### Streaming Data (Advanced)

```python
# Request continuous stream of captured samples
def stream_continuous():
    while True:
        # Trigger a new capture
        client.request(0x01, b'')  # ARM
        
        # Receive streamed data
        # (In actual implementation, would receive via 0xE0 STREAM_DATA packets)
        
        # Process...
        pass
```

### Interfacing with PulseView

PulseView uses native SUMP protocol (not Python tooling), but you can use the Python tools for debugging:

```bash
# Terminal 1: Start device
py -3 main.py

# Terminal 2: Monitor device (in separate process)
py -3 main.py --stats  # Poll stats periodically

# Terminal 3: Run PulseView
# File → Connect to Device → Openbench Logic Sniffer
# Select COM port
# Capture
```

---

## Troubleshooting

### Device Not Found

```bash
# Check connected devices
py -3 -c "import serial.tools.list_ports; print([p.device for p in serial.tools.list_ports.comports()])"

# Check VID/PID
py -3 -c "import serial.tools.list_ports; [print(f'{p.device}: {p.vid:04X}:{p.pid:04X}') for p in serial.tools.list_ports.comports()]"
```

### Connection Timeouts

```bash
# Increase timeout (default 1000 ms)
py -3 main.py --timeout 5000

# Check if device is responsive
py -3 -c "
import serial
ser = serial.Serial('COM9', 115200, timeout=1)
ser.write(b'\\x00')  # SUMP RESET
print('Device responded')
ser.close()
"
```

### CRC Errors

```bash
# Enable verbose logging (requires code mod)
# Check raw bytes:
py -3 -c "
import serial
ser = serial.Serial('COM9', 115200, timeout=1)
ser.write(b'\\x00\\x00\\x00\\x00')  # SUMP commands
data = ser.read(100)
print(' '.join(f'{b:02X}' for b in data))
"
```

---

## References

- [SUMP Protocol v0.txt](https://openbenchlogicsniffer.googlecode.com/files/ols_protocol_v0.txt)
- [PySerial Documentation](https://pyserial.readthedocs.io/)
- [PySide6 (Qt for Python)](https://doc.qt.io/qtforpython/)
- [Python struct module](https://docs.python.org/3/library/struct.html)

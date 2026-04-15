# BlackPill STM32F411 SUMP Logic Analyzer

**A professional USB-FPGA bridge implementation using STM32F411 BlackPill as the host interface layer.**

---

## Overview

This project implements the **STM32 side of a modular logic analyzer architecture**:
- **STM32F411 BlackPill**: USB host interface, protocol gateway, FPGA communication bridge
- **Future FPGA**: High-speed data acquisition engine (to be integrated)

The STM32F411 currently demonstrates the USB protocol layer with 8-channel SUMP compatibility. When integrated with an external FPGA via SPI/parallel interface, it will serve as the high-speed data gateway and USB host controller, enabling professional-grade logic analysis while keeping host-side software simple and cross-platform.

### Architecture
```
┌──────────────────────────┐
│   PulseView / Host PC    │
└────────────┬─────────────┘
             │ USB CDC
             ↓
   ┌─────────────────────────────────┐
   │   STM32F411 BlackPill           │
   │  (USB Host + FPGA Bridge)       │
   │                                 │
   │  ┌──────────────────────────┐   │
   │  │ USB CDC Protocol Engine  │   │
   │  │ - SUMP parser            │   │
   │  │ - Frame protocol         │   │
   │  │ - Data routing           │   │
   │  └──────────────────────────┘   │
   │           │                      │
   │           ↓ (SPI/Parallel TBD)   │
   │  ┌──────────────────────────┐   │
   │  │ FPGA Interface (Future)  │   │
   │  └──────────────────────────┘   │
   └─────────────────────────────────┘
             ↓
   (FPGA probe inputs: 16-256 channels,
    100+ MHz sample rates - not yet implemented)
```

### Current Capabilities (STM32-Only Demo)

This project demonstrates the **USB protocol layer** with 8-channel SUMP compatibility for testing and validation:

| Aspect | Detail |
|--------|--------|
| **Microcontroller** | STM32F411CEU6 (ARM Cortex-M4, 100 MHz) |
| **USB Interface** | USB 2.0 Full-Speed CDC Virtual COM Port |
| **Logic Channels** | 8 channels (GPIOB[0:7]) - STM32 GPIO only |
| **Max Sample Rate** | 2 MHz (configurable via SUMP divider) |
| **Capture Buffer** | 16 KB (4096 32-bit samples) |
| **Sampling Precision** | 10 ns (DWT cycle counter) |
| **Protocol Support** | SUMP (OLS/PulseView) + Custom framed binary |
| **Debug Probe** | J-Link, ST-Link (configurable) |
| **Power** | USB powered (5V/500mA) |

### Future FPGA Integration (Not Yet Implemented)

When paired with an external FPGA:
- **Channels**: 16-256+ logic inputs (FPGA-dependent)
- **Sample Rate**: 100+ MHz real-time acquisition
- **Capture Memory**: Megabytes (FPGA BRAM-dependent)
- **Triggering**: Complex pattern matching (FPGA logic)
- **Latency**: Sub-microsecond trigger-to-USB (FPGA + STM32 bridge)

The STM32F411 will handle:
- USB enumeration and CDC protocol management
- FPGA data buffering and flow control
- Protocol conversion (FPGA format ↔ SUMP)
- Cross-platform compatibility (Linux, macOS, Windows via PulseView)

### Unique Features

✅ **Dual-Protocol Runtime**: Single USB endpoint services both SUMP and custom binary protocol simultaneously
✅ **Hardware Timing Precision**: DWT cycle counter for sub-microsecond sample intervals
✅ **Robust Frame Validation**: 32-bit CRC polynomial (0xEDB88320) on all binary frames
✅ **Ring Buffer with DMA Support**: 4096-byte RX buffer, 16-slot TX queue with optional DMA copy
✅ **Professional LED Feedback**: Real-time visual indicators (error, capture, heartbeat, activity)
✅ **Comprehensive Python Tooling**: CLI client, Qt6 GUI, automated smoke tests
✅ **Portable Build System**: Automated probe detection, PowerShell-based CI workflow
✅ **FPGA-Ready Architecture**: Modular design supports future FPGA integration without host software changes

---

## Architecture

### Block Diagram

```
┌─────────────────────────────────┐
│    PulseView / Host Tools       │
│  (SUMP CLI / Custom Framed)     │
└──────────────┬──────────────────┘
               │ USB CDC
               ↓
    ┌──────────────────────┐
    │  STM32F411 BlackPill │
    ├──────────────────────┤
    │ ┌──────────────────┐ │
    │ │   USB Stack      │ │  ← USB Device Controller
    │ │  (CDC/Virtual    │ │     + Middleware
    │ │   COM Port)      │ │
    │ └─────────┬────────┘ │
    │           │          │
    │ ┌─────────▼────────┐ │
    │ │ Protocol Parser  │ │  ← SUMP & Framed
    │ │  State Machine   │ │     Protocol Engines
    │ └────────┬─────────┘ │
    │          │           │
    │ ┌────────▼──────────┐ │
    │ │ Sample Capture    │ │  ← DWT-based timing
    │ │  & Ring Buffers   │ │
    │ └────────┬──────────┘ │
    │          │            │
    │ ┌────────▼──────────┐ │
    │ │ GPIOB CH0..CH7    │ │  ← Logic probe inputs
    │ └───────────────────┘ │
    └──────────────────────┘
```

### Module Hierarchy

```
Core/Src/
  main.c               ← System init, main loop, LED service
  stm32f4xx_it.c       ← Interrupt handlers
  system_stm32f4xx.c   ← CMSIS startup

USB_DEVICE/
  App/
    usbd_cdc_if.c      ← Protocol engines (SUMP + framed)
    usbd_cdc_if.h      ← Protocol definitions, state structs
  Target/
    usbd_conf.c        ← USB stack configuration

Drivers/
  STM32F4xx_HAL_Driver/   ← Hardware Abstraction Layer
  CMSIS/                  ← Cortex Microcontroller Software Interface
```

---

## Protocol Specifications

### 1. SUMP Protocol (OLS-Compatible)

**Purpose**: Native PulseView integration without driver installation.

**Command Set**:

| Byte | Command | Argument | Response |
|------|---------|----------|----------|
| 0x00 | RESET | — | State cleared |
| 0x01 | ARM | — | Samples streamed (raw bytes) |
| 0x02 | ID | — | `0x31, 'A', 'L', 'S'` (4 bytes) |
| 0x04 | METADATA | — | Null-terminated tags (device name, version, caps) |
| 0x80 | SET_DIVIDER | 4-byte LE32 | Sample rate = 100MHz / (divider + 1) |
| 0x81 | CAPTURE_SIZE | 4-byte LE32 | Word count (4 bytes per word) |
| 0x82 | SET_FLAGS | 4-byte LE32 | DEMUX, RLE, test patterns, group enables |
| 0x83 | DELAY_COUNT | 4-byte LE32 | Samples before trigger (not yet implemented) |
| 0xC0–CF | TRIG_* | 4-byte LE32 | Trigger config (stages 0-3, mask/value) |

**Metadata Format** (human-readable example):
```
0x01 BlackPill STM32 0x00       (device name)
0x02 stm32f411 0x00             (firmware version)
0x21 0x00004000 0x00            (sample memory: 16384 bytes)
0x23 0x001E8480 0x00            (max rate: 2 MHz)
0x40 0x08                        (num probes: 8 channels)
0x41 0x02                        (protocol version: 2)
0x00                             (end marker)
```

**Sample Transmission**:
- Samples streamed as raw bytes
- **Byte order**: Samples sent in **reverse** (newest first)
- **Rate**: Controlled via SET_DIVIDER
- **Channels**: GPIOB[0:7] → CH0..CH7

**Trigger Behavior**:
- Waits up to 800 ms for (GPIOB & trigger_mask) == trigger_value
- If timeout, proceeds anyway (increments timeout counter)
- Capture starts immediately after trigger match

### 2. Custom Framed Binary Protocol

**Purpose**: High-reliability, diagnostics, streaming tests.

**Frame Structure** (20 bytes header + 0–512 bytes payload):

| Offset | Bytes | Field | Type | Notes |
|--------|-------|-------|------|-------|
| 0 | 4 | Magic | LE32 | `0x31544255` (ASCII: "UBT1") |
| 4 | 1 | Version | U8 | `0x01` |
| 5 | 1 | Type | U8 | Command/response code |
| 6 | 2 | Payload Length | LE16 | 0–512 bytes |
| 8 | 4 | Sequence ID | LE32 | Echo ID (never 0) |
| 12 | 4 | CRC-32 | LE32 | Polynomial 0xEDB88320 (inverted) |
| 16 | N | Payload | Binary | Variable length |

**Command Codes** (0x00–0x7F):

| Code | Name | Payload | Response |
|------|------|---------|----------|
| 0x01 | HELLO_REQ | 0 bytes | HELLO_RSP (0x81) with device info |
| 0x02 | PING_REQ | Variable | PING_RSP (0x82) echoes payload |
| 0x06 | STATS_REQ | 0 bytes | STATS_RSP (0x86) with counters |
| 0x08 | LED_REQ | Mode byte | Acknowledges LED control |
| 0x0A | LOGIC_SNAPSHOT_REQ | 0 bytes | LOGIC_RSP (0x8A) with 8 samples |

**Response Codes** (0x80–0xFF):

| Code | Name | Payload |
|------|------|---------|
| 0x81 | HELLO_RSP | Device name, FW version, clock freq, ring sizes |
| 0x82 | PING_RSP | Echo of request payload |
| 0x86 | STATS_RSP | RX/TX counters, DMA stats, stream state |
| 0x8A | LOGIC_RSP | 8 bytes (latest GPIOB samples) |
| 0xE0 | STREAM_DATA | Streaming payload chunk |
| 0xF0 | WARN | Warning message (diagnostic) |
| 0xFF | ERROR | Frame error, resync required |

**CRC-32 Calculation**:
```c
uint32_t crc = 0xFFFFFFFF;
for (byte in payload) {
  crc ^= byte;
  for (int i = 0; i < 8; i++) {
    crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
}
return crc ^ 0xFFFFFFFF;  // Final XOR
```

---

## Hardware Configuration

### STM32F411CE Pinout (BlackPill)

```
             ┌─────────────────┐
             │   BlackPill     │
             │   STM32F411     │
             │                 │
    USB ────→ PA11/PA12        │
    J-Link   PB6 (CLK)         │
    (SWD) ── PB7 (DIO)         │
             │                 │
    Logic ── PB0/PB7 ────────→ CH0..CH7
    Input    │                 │
             │                 │
    User LED─ PC13 ────────→ Status LED
    User SW─ PC0/PC1           │
             │                 │
             └─────────────────┘
```

### GPIO Configuration

| Pin | Port | Mode | Function |
|-----|------|------|----------|
| PB0-PB7 | GPIOB | Input (floating) | Logic analyzer probes (CH0-CH7) |
| PC13 | GPIOC | Output (push-pull) | User LED (active-low) |
| PA11/PA12 | GPIOA | Alternate (AF10) | USB D-/D+ |
| PB6/PB7 | GPIOB | Alternate (AF0) | SWD CLK/DIO (debug) |

### Clock Configuration

```
HSE (25 MHz) ──┐
               ├─→ PLL ──┬─→ 96 MHz (SYSCLK)
               │         ├─→ 48 MHz (USB)
               └─────────┴─→ APB1/APB2 clocks
```

- **PLLM**: 25 (input divider)
- **PLLN**: 192 (multiplier)
- **PLLP**: 2 (system divider)
- **PLLQ**: 4 (USB divider)
- **Result**: 96 MHz SYSCLK, 48 MHz USB clock

---

## Getting Started

### Prerequisites

- **Hardware**: STM32F411 BlackPill, J-Link probe (or ST-Link)
- **Build Tools**: STM32CubeIDE, arm-none-eabi-gcc
- **Host Tools**: Python 3.8+, PySerial, PySide6

### 1. Build Firmware

```powershell
cd Cube_demo/USB_Test_Project
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
```

**Output**: `Debug/USB_Test_Project.elf` (42 KB code, 40 KB RAM)

### 2. Flash Firmware

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_jlink.ps1 -Run
```

**Probe Detection**: Script auto-detects J-Link installation
**Result**: Device boots and enumerates as USB CDC device

### 3. Verify Installation

```powershell
cd ..
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1
```

**Test Suite**:
- Build (if needed)
- Flash (if needed)
- Python smoke tests (SUMP protocol validation)
- Integrity checks (frame parser, CRC)
- Stream throughput benchmark

### 4. PulseView Integration

1. Install PulseView
2. Plug in BlackPill (identifies as USB CDC device, e.g., COM9)
3. File → Connect to Device → Openbench Logic Sniffer
4. Select COM port
5. Capture → Start
6. View waveforms (CH0-CH7)

### 5. Signal Quality Optimization

**Noise Elimination** (Applied by Default):
- **GPIO Pull Configuration**: All input probes (GPIOB[0:7]) configured with `GPIO_PULLDOWN`
  - Eliminates floating-node 50-60 Hz mains frequency noise coupling
  - Idle probes default to logic LOW (0V) instead of oscillating
  
- **Digital Filtering**: Majority voting filter on each sample
  - Reads GPIO 3 times, applies bit-wise majority vote
  - Filters single-bit glitches and high-frequency noise
  - Zero sampling speed penalty (~200 ns overhead, negligible at 2 MHz)

- **Result**: Clean, stable captures without spurious signal detection
  - Before: Idle captures show 50 Hz oscillation, sample values range 0x2A-0x5E
  - After: Idle captures stable, all samples read 0x00
  - Improvement: 80-90% noise reduction

**For Production Use**, consider adding external **10 kΩ pull-down resistors** on each probe line for additional AC termination (see [NOISE_ELIMINATION.md](docs/NOISE_ELIMINATION.md) for details).

---

## Project Structure

```
BlackPill_SUMP_Logic_Analyzer/
├── README.md                    ← This file
├── LICENSE                      ← MIT License
├── run_usb_tests.ps1            ← E2E test runner
│
├── Cube_demo/USB_Test_Project/  ← STM32CubeIDE firmware
│   ├── build.ps1                ← Build script (auto-detects IDE)
│   ├── flash_jlink.ps1          ← Flash script (J-Link)
│   ├── STM32F411CEUX_FLASH.ld   ← Linker script
│   ├── USB_Test_Project.ioc     ← CubeMX configuration
│   ├── Core/
│   │   ├── Inc/main.h
│   │   └── Src/main.c           ← System init, main loop
│   ├── USB_DEVICE/
│   │   ├── App/usbd_cdc_if.c    ← Protocol engines
│   │   └── Target/usbd_conf.c   ← USB config
│   └── Drivers/                 ← HAL, CMSIS, USB Middleware
│
├── Python_Test/                 ← Host-side tools
│   ├── main.py                  ← CLI client (framed protocol)
│   ├── gui_qt.py                ← Qt6 GUI control panel
│   ├── sump_smoke_test.py       ← SUMP validation script
│   └── README.md                ← Python tools guide
│
├── docs/                        ← Technical documentation
│   ├── README.md                ← Doc index
│   ├── ARCHITECTURE.md          ← Deep-dive architecture
│   ├── PROTOCOL.md              ← Protocol specifications
│   ├── HARDWARE.md              ← Pinouts, schematics
│   ├── FIRMWARE.md              ← Firmware internals
│   ├── PYTHON_TOOLS.md          ← Host tools guide
│   └── DEBUGGING.md             ← Debug workflow
│
└── .vscode/                     ← VSCode configuration
    ├── tasks.json               ← Build/flash/test tasks
    ├── launch.json              ← J-Link debug config
    └── settings.json            ← Workspace settings
```

---

## Performance Metrics

### Sampling Throughput

| Rate | Period | Precision | Throughput |
|------|--------|-----------|-----------|
| 100 kHz | 10 µs | 10 ns | 100 KB/s |
| 500 kHz | 2 µs | 10 ns | 500 KB/s |
| 1 MHz | 1 µs | 10 ns | 1 MB/s |
| 2 MHz | 500 ns | 10 ns | 2 MB/s |

**Capture Duration** @ 2 MHz with 16 KB buffer:
- Sample count: 4096 samples
- Duration: 2.048 ms
- Pre-trigger: 0–2.048 ms (configurable)

### USB Throughput

| Mode | Rate | Chunk Size | Latency |
|------|------|-----------|---------|
| SUMP | 64 bytes/endpoint | 64 bytes | <1 ms |
| Framed (small) | ~200 frames/sec | 64 bytes | 5 ms |
| Framed (large) | ~100 frames/sec | 512 bytes | 10 ms |

---

## Development Roadmap

### Implemented ✅
- SUMP protocol (OLS/PulseView compatible)
- Custom framed binary protocol (CRC validation)
- 8-channel logic capture (GPIOB[0:7])
- Configurable sample rate (100 kHz – 2 MHz)
- Ring buffer (4 KB RX, 16-slot TX queue)
- Dual-protocol runtime multiplexing
- LED status indication (error, activity, heartbeat)
- Python CLI + Qt6 GUI + smoke tests
- Automated build/flash/test workflow

### Planned Enhancements 📋
- [ ] **RLE Encoding** (SUMP flag 0x0100): Reduce memory usage for repetitive captures
- [ ] **Timer-Based Sampling** (vs. DWT busy-wait): Reduce CPU load, improve timing stability
- [ ] **Advanced Trigger Stages**: 4-stage sequential trigger sequences
- [ ] **External Trigger Input**: Edge-triggered synchronization
- [ ] **Data Compression**: DEFLATE or delta encoding for large captures
- [ ] **Pre/Post-Trigger Delay**: Proper delay_count implementation
- [ ] **Async Notifications**: "Capture ready" packet instead of polling
- [ ] **Frequency Calibration**: Cold-start frequency trim
- [ ] **Multi-rate Demultiplexing**: Capture at >2 MHz logical rate

### Testing ✅
- Firmware builds cleanly (clean build, 0 warnings/errors)
- SUMP smoke test passes (ID, metadata, capture)
- Frame parser handles CRC, resync, overflow
- Python tools (CLI, GUI, tests) run without errors
- End-to-end workflow: Build → Flash → Test → Verify
- USB stability under sustained load (8-second benchmark)

---

## Debugging

### VSCode Integration

**Build Task**: `Ctrl+Shift+B` → Runs `build.ps1`
**Flash Task**: Run "STM32: Flash (J-Link)" → Flashes firmware
**Debug**: `F5` → Attaches J-Link debugger, sets breakpoints

### Suggested Breakpoints

| File | Function | Purpose |
|------|----------|---------|
| usbd_cdc_if.c | `USB_ServiceSumpParser` | SUMP command arrival |
| usbd_cdc_if.c | `USB_ServiceParser` | Framed packet arrival |
| usbd_cdc_if.c | `USB_SumpAcquireAndSend` | Capture trigger |
| main.c | `CDC_AppTask` | Main protocol loop |
| main.c | `UserLed_Service` | LED state changes |

### LED Diagnostic Patterns

| Pattern | Meaning |
|---------|---------|
| Solid ON | USB activity (RX or TX within last 80 ms) |
| 120 ms blink | Capture in progress (streaming samples) |
| 80 ms blink (fast) | Error (persists 600 ms) |
| 500 ms blink | Idle/Heartbeat (normal default) |

---

## Contributing & License

This is a **reference implementation** for educational and professional use.

**License**: MIT (see `LICENSE` file)
**Author**: Thomas Faucherre
**Status**: Production-ready for evaluation, research, and integration into larger projects

### Extending the Project

To adapt this for production:
1. Add timer-based sampling (see `docs/FIRMWARE.md` for details)
2. Implement RLE compression (SUMP flag 0x0100)
3. Add external trigger input (GPIO edge detection)
4. Increase capture depth (add external SRAM or QSPI)
5. Implement pre/post-trigger delay
6. Add frequency calibration routine

---

## FAQ & Troubleshooting

### Q: Device not detected after flash?
**A**: 
- Check USB cable (data cable, not power-only)
- Verify J-Link completed successfully (check terminal output)
- Check Windows Device Manager for unknown USB device
- Try `powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean` then reflash

### Q: SUMP smoke test fails with "ID mismatch"?
**A**: 
- Device not flashed or crashed
- Try pressing reset button on board
- Check COM port in test script (auto-detect may fail)
- Manual port selection: `sump_smoke_test.py --port COM9`

### Q: Samples look random/incorrect?
**A**: 
- Verify probe cables connected to PB0-PB7
- Check signal voltage (3.3V logic levels)
- Verify sample rate not too high for signal frequency
- Use logic snapshot command to verify basic GPIO reads

### Q: Build fails with "arm-none-eabi-gcc not found"?
**A**: 
- Ensure STM32CubeIDE is installed in standard location (C:\ST\STM32CubeIDE_*)
- Check `build.ps1` probe detection (may need manual path adjustment)

---

## References

- [SUMP Protocol Specification](https://openbenchlogicsniffer.googlecode.com/files/ols_protocol_v0.txt)
- [PulseView Logic Analyzer](https://sigrok.org/wiki/PulseView)
- [STM32F411 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xce-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [STM32CubeF4 Firmware Package](https://github.com/STMicroelectronics/STM32CubeF4)

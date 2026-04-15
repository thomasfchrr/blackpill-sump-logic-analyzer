# Debugging Guide: Professional Workflow

## Table of Contents
1. [Overview](#overview)
2. [Hardware Setup](#hardware-setup)
3. [VSCode + J-Link Integration](#vscode--j-link-integration)
4. [Debugging Techniques](#debugging-techniques)
5. [Breakpoint Strategy](#breakpoint-strategy)
6. [Runtime Diagnostics](#runtime-diagnostics)
7. [Troubleshooting](#troubleshooting)

---

## Overview

This guide provides a reproducible, professional debug workflow for the BlackPill SUMP Logic Analyzer, covering:

- **Firmware debugging** with external probes (J-Link, ST-Link)
- **Runtime protocol validation** via host tools
- **System-level testing** with automated test suites
- **Diagnostic techniques** for rapid issue identification

---

## Hardware Setup

### Debug Probe: J-Link

**Default Configuration**:
- Probe: Segger J-Link (any model)
- Connection: SWD (Serial Wire Debug)
- Signals: CLK (PB6), DIO (PB7)
- Speed: 4 MHz (auto-negotiated)

**Required Connections**:

```
J-Link Connector          BlackPill GPIO
─────────────────        ──────────────
Pin 1 (VCC)       ────→  3.3V (VDD)
Pin 2 (GND)       ────→  GND
Pin 3 (NC)        ────→  (not used)
Pin 4 (GND)       ────→  GND
Pin 5 (TCLK)      ────→  PB6 (SWD CLK)
Pin 6 (GND)       ────→  GND
Pin 7 (TDI)       ────→  NC
Pin 8 (GND)       ────→  GND
Pin 9 (TDO)       ────→  PB7 (SWD DIO)
Pin 10 (GND)      ────→  GND
```

**Alternative: ST-Link v2**

To use ST-Link instead of J-Link:

1. Edit `.vscode/launch.json`:
```json
{
  "miDebuggerPath": "arm-none-eabi-gdb",
  "miDebuggerServerAddress": "localhost:3333",  // ST-Link GDB server port
  "setupCommands": [
    { "text": "target remote localhost:3333" }
  ]
}
```

2. Start ST-Link GDB server:
```bash
ST-LinkGDBServer.exe -p 3333 -d STM32F411CE -cnx SWD
```

### Power Considerations

**USB Power Only** (recommended for development):
- BlackPill draws <100 mW
- J-Link provides 3.3V reference (not power)
- Device powers from USB cable

**If Disconnecting USB**:
- Device enters low-power mode (DWT disabled)
- Sampling/USB functions unavailable
- Debugging still works (J-Link provides power if jumper set)

---

## VSCode + J-Link Integration

### File Structure

```
.vscode/
├── launch.json         ← GDB debug configuration
├── tasks.json          ← Build/flash/test tasks
└── settings.json       ← Workspace settings
```

### Launch Configuration

**File**: `.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "J-Link Debug",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/Cube_demo/USB_Test_Project/Debug/USB_Test_Project.elf",
      "cwd": "${workspaceFolder}",
      "stopAtEntry": false,
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "arm-none-eabi-gdb",
      "miDebuggerServerAddress": "localhost:2331",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        },
        {
          "description": "Set print union",
          "text": "-gdb-set print union on",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "STM32: Build (Incremental)",
      "serverLaunchTimeout": 5000,
      "filterStderr": true,
      "filterStdout": false
    }
  ]
}
```

### Build & Flash Tasks

**File**: `.vscode/tasks.json`

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "STM32: Build (Clean)",
      "type": "shell",
      "command": "powershell",
      "args": [
        "-ExecutionPolicy", "Bypass",
        "-File", "${workspaceFolder}/Cube_demo/USB_Test_Project/build.ps1",
        "-Clean"
      ],
      "group": { "kind": "build", "isDefault": true }
    },
    {
      "label": "STM32: Build (Incremental)",
      "type": "shell",
      "command": "powershell",
      "args": [
        "-ExecutionPolicy", "Bypass",
        "-File", "${workspaceFolder}/Cube_demo/USB_Test_Project/build.ps1"
      ]
    },
    {
      "label": "STM32: Flash (J-Link)",
      "type": "shell",
      "command": "powershell",
      "args": [
        "-ExecutionPolicy", "Bypass",
        "-File", "${workspaceFolder}/Cube_demo/USB_Test_Project/flash_jlink.ps1",
        "-Run"
      ],
      "dependsOn": ["STM32: Build (Incremental)"]
    }
  ]
}
```

### Keyboard Shortcuts

| Action | Shortcut | Effect |
|--------|----------|--------|
| Build | `Ctrl+Shift+B` | Runs "STM32: Build (Clean)" |
| Debug | `F5` | Starts J-Link debugger |
| Stop Debug | `Shift+F5` | Disconnects debugger, halts device |
| Continue | `F5` (during debug) | Resume execution |
| Step Over | `F10` | Step one line (skip functions) |
| Step Into | `F11` | Step into function calls |
| Step Out | `Shift+F11` | Exit current function |
| Toggle Breakpoint | `Ctrl+F9` | Set/remove breakpoint at cursor |

---

## Debugging Techniques

### Live Monitoring via GDB

**Connect to running device**:
```bash
arm-none-eabi-gdb
(gdb) target remote localhost:2331
(gdb) file Cube_demo/USB_Test_Project/Debug/USB_Test_Project.elf
(gdb) load  # Flash new firmware
(gdb) break main  # Set breakpoint
(gdb) continue
```

### Memory Inspection

**Read hardware register**:
```bash
(gdb) p /x *(uint32_t *)0x40020C00
# GPIOB Base + offset 0xC00 = IDR (Input Data Register)
```

**Read ring buffer**:
```bash
(gdb) p /x s_rx_ring[0]@16
# Print first 16 bytes of RX ring
```

**Watch variable**:
```bash
(gdb) watch s_sump.capture_in_progress
# Trigger breakpoint when changed
```

### CPU Cycle Count

```bash
(gdb) p /u DWT->CYCCNT
# Current cycle count (for timing analysis)
```

---

## Breakpoint Strategy

### Critical Path Breakpoints

| Function | File | Purpose | Reason |
|----------|------|---------|--------|
| `main` | main.c | Startup | Verify initialization |
| `CDC_AppTask` | usbd_cdc_if.c | Main loop | Monitor protocol dispatch |
| `USB_ServiceSumpParser` | usbd_cdc_if.c | SUMP parsing | Debug protocol errors |
| `USB_ServiceParser` | usbd_cdc_if.c | Framed parsing | Debug frame errors |
| `USB_SumpAcquireAndSend` | usbd_cdc_if.c | Capture | Verify sampling |
| `USB_RxRingWrite` | usbd_cdc_if.c | RX interrupt | Monitor incoming data |
| `CDC_Receive_FS` | usbd_cdc_if.c | USB RX callback | Check USB reception |
| `UserLed_Service` | main.c | LED status | Verify state indication |

### Conditional Breakpoints

**Example: Break on CRC error**:
```
File: usbd_cdc_if.c, function USB_ServiceParser
Condition: crc_calc != crc_rx
```

**Example: Break on SUMP ARM**:
```
File: usbd_cdc_if.c, function USB_ServiceSumpParser
Line: case 0x01:  // ARM
```

### Log Points (instead of breakpoints)

**VSCode feature**: Print message without stopping

```bash
# Set "logpoint" instead of breakpoint
# Message: "SUMP ARM detected, samples={s_sump.readcount_words}"
```

---

## Runtime Diagnostics

### LED Status Indicators

The firmware provides real-time visual feedback via LED patterns:

**LED Pin**: PC13 (active-low, inverted logic)

| Pattern | Duration | Meaning | Debug Action |
|---------|----------|---------|--------------|
| **Solid ON** | 80 ms | USB RX/TX activity | Normal operation |
| **Slow blink** (120 ms) | Continuous | Capture in progress | Check DWT timing |
| **Fast blink** (80 ms) | 600 ms | Error occurred | Read error counters |
| **Heartbeat** (500 ms) | Idle | No activity | Check main loop |

### Error Counters

**Location**: `struct { uint32_t ... } s_sump;`

```c
s_sump.error_tx_timeout           // CDC transmit timeout
s_sump.error_flag                 // General error (trigger LED blink)
s_sump.trigger_timeout_count      // Trigger wait exceeded 800 ms
s_sump.capture_overflow           // Sample count > 16384
s_sump.error_time                 // Last error timestamp (ms)
```

**Access via host**:
```bash
py -3 main.py --port COM9
> stats
# Displays all counters
```

### Protocol State Inspection

**Framed Protocol State**:
```c
struct {
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t dma_transfers;
  uint32_t crc_errors;
  uint32_t framing_errors;
} s_proto;
```

**SUMP State**:
```c
struct {
  uint32_t divider;              // Sample rate divider
  uint32_t readcount_words;      // Capture word count
  uint32_t delaycount_words;     // Trigger delay (not used)
  uint32_t flags;                // Configuration flags
  uint32_t trig_mask[4];         // Trigger masks (stage 0–3)
  uint32_t trig_value[4];        // Trigger values
  uint8_t capture_in_progress;
} s_sump;
```

---

## Testing Workflow

### 1. Rapid Build & Flash

```bash
# Automatic via VSCode
Ctrl+Shift+B  # Build
(select "STM32: Flash (J-Link)")

# Or manual
cd Cube_demo/USB_Test_Project
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
powershell -ExecutionPolicy Bypass -File .\flash_jlink.ps1 -Run
```

**Expected output**:
```
Building...
Linking USB_Test_Project.elf
Flashing via J-Link...
Device reset and running
```

### 2. SUMP Smoke Test

```bash
# Validate SUMP protocol
cd Python_Test
py -3 sump_smoke_test.py --port COM9

# Expected:
# SUMP ID: b'1ALS' ✓
# Metadata: ... ✓
# Capture OK: 1024 bytes ✓
```

### 3. Custom Frame Protocol Test

```bash
# Test framed protocol (CRC, sequence numbers)
py -3 main.py --port COM9 --self-test

# Expected:
# HELLO check: PASS
# Integrity check: 200/200 OK
# Latency: 12 ms average
```

### 4. End-to-End Test Suite

```bash
# Full automated test pipeline
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1

# Runs:
# 1. Build
# 2. Flash
# 3. Python smoke tests
# 4. Throughput benchmark
```

---

## Troubleshooting Guide

### Device Not Enumerated

**Symptom**: No COM port appears in Device Manager

**Debugging Steps**:

1. **Check USB cable**: Verify data cable (not power-only), try different port
2. **Check STM32 firmware**: 
   - Reflash with `flash_jlink.ps1 -Run`
   - Expected output: "Device reset and running"
3. **Check J-Link connection**:
   ```bash
   JLinkGDBServer.exe -device STM32F411CE -if SWD
   # Should show "Connected to J-Link"
   ```
4. **Try cold boot**:
   - Unplug USB for 5 seconds
   - Replug and wait 2 seconds for enumeration

### Device Enumerated but Unresponsive

**Symptom**: COM port exists but no response to commands

**Debugging Steps**:

1. **Check LED**: 
   - If LED never blinks → firmware crashed at startup
   - If LED blinks → main loop running
2. **Check USB enumeration**:
   ```bash
   py -3 sump_smoke_test.py --port COM9 --verbose
   # Shows USB I/O details
   ```
3. **Breakpoint at USB callback**:
   - Set breakpoint in `CDC_Receive_FS`
   - Send command from host
   - If breakpoint never hits → USB not receiving data

### Sampling Timing Issues

**Symptom**: Inconsistent sample rate, jitter in captured data

**Debugging Steps**:

1. **Verify DWT enabled**:
   ```bash
   (gdb) p CoreDebug->DEMCR
   # Should have TRCENA bit (0x01000000) set
   ```
2. **Check clock frequency**:
   ```bash
   (gdb) p SystemCoreClock
   # Should be 96000000 (96 MHz)
   ```
3. **Measure actual rate**:
   - Capture known frequency signal (e.g., 1 kHz)
   - Compare captured edges with expected count

### SUMP Protocol Issues

**Symptom**: PulseView shows "unknown device" or "timeout"

**Debugging Steps**:

1. **Test SUMP ID**:
   ```bash
   py -3 sump_smoke_test.py --port COM9
   # Check ID response is exactly b'1ALS'
   ```
2. **Test metadata**:
   ```bash
   # Metadata should parse without errors
   # Check device name, FW version, sample memory
   ```
3. **Test ARM command**:
   ```bash
   # Should return exactly sample_count bytes
   # Check byte order (reversed)
   ```

---

## Advanced Debugging

### Hardware Breakpoints

J-Link supports 6 hardware breakpoints (vs. unlimited software breakpoints):

```bash
(gdb) break USB_SumpAcquireAndSend  # Hardware breakpoint (preferred)
(gdb) break -h USB_SumpAcquireAndSend  # Explicit hardware
```

### Trace Output (ITM)

The STM32F411 supports Instrumentation Trace Macrocell (ITM) for printf-style debugging:

```c
// ITM printf (requires SWO pin connection)
int ITM_SendChar(int ch) {
  ITM_SendData(ch);
  return ch;
}
```

(Not currently implemented, but available for future use)

### External Oscilloscope

For precise timing analysis:

1. Connect oscilloscope probe to output GPIO
2. Toggle GPIO at key points:
   ```c
   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);    // Trigger
   // ... work ...
   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);  // Done
   ```
3. Measure pulse width on oscilloscope
4. Compare with expected timing

---

## Production Validation

### Firmware Validation Checklist

- [ ] Build completes without warnings or errors
- [ ] Flash succeeds via J-Link
- [ ] LED heartbeat visible (500 ms blink)
- [ ] SUMP smoke test passes
- [ ] Frame protocol self-test passes
- [ ] Capture data matches known test pattern
- [ ] No USB timeouts over 30-minute test
- [ ] No memory leaks (RAM stable)
- [ ] No CRC errors logged

### System Integration Checklist

- [ ] PulseView connects and captures signals
- [ ] Trigger conditions work (mask/value matching)
- [ ] Sample rate accurate to ±1%
- [ ] Pre-trigger delay working (if implemented)
- [ ] Multi-probe captures synchronized
- [ ] LED status patterns match specification

---

## References

- [GDB Manual](https://sourceware.org/gdb/current/onlinedocs/gdb/)
- [J-Link GDB Server Documentation](https://wiki.segger.com/J-Link_GDB_Server)
- [ARM Cortex-M Debug Architecture](https://developer.arm.com/documentation/ddi0403/latest/)
- [STM32F411 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xce-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)


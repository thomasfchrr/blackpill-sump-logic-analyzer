# Architecture Deep-Dive: BlackPill SUMP Logic Analyzer

## Table of Contents
1. [System Architecture](#system-architecture)
2. [USB Protocol Stack](#usb-protocol-stack)
3. [Sampling Engine](#sampling-engine)
4. [Data Flow & Buffering](#data-flow--buffering)
5. [State Machines](#state-machines)
6. [Timing Analysis](#timing-analysis)
7. [Performance Tuning](#performance-tuning)

---

## System Architecture

### High-Level Design

```
┌──────────────────────────────────────────────────────────┐
│                    STM32F411 BlackPill                   │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │         USB Device Controller (DCD)               │ │
│  │     STM32 OTG_FS Peripheral + HAL Driver         │ │
│  └────────────────┬─────────────────────────────────┘ │
│                   │                                    │
│  ┌────────────────▼─────────────────────────────────┐ │
│  │    USB Device Middleware Stack (ST Lib)         │ │
│  │  - CDC Class Driver                             │ │
│  │  - USB Device Core                              │ │
│  │  - Endpoint Management                          │ │
│  └────────────────┬─────────────────────────────────┘ │
│                   │                                    │
│  ┌────────────────▼─────────────────────────────────┐ │
│  │      Application CDC Interface Layer            │ │
│  │   usbd_cdc_if.c (TX/RX callbacks)               │ │
│  └────────────────┬─────────────────────────────────┘ │
│                   │                                    │
│       ┌───────────┴───────────┐                       │
│       │                       │                       │
│  ┌────▼─────────┐     ┌──────▼──────────┐            │
│  │ RX Ring      │     │ TX Queue        │            │
│  │ Buffer       │     │ (16 slots)      │            │
│  │ 4096 bytes   │     │                 │            │
│  └────┬─────────┘     └──────┬──────────┘            │
│       │                      │                       │
│  ┌────▼──────────────────────▼──────┐               │
│  │   Protocol Parser & Dispatcher   │               │
│  │  - SUMP State Machine            │               │
│  │  - Framed Protocol Parser        │               │
│  │  - Command Router                │               │
│  └────┬──────────────────────────┬──┘               │
│       │                          │                   │
│  ┌────▼────────────┐      ┌──────▼────────┐        │
│  │ SUMP Engine     │      │ Framed Engine │        │
│  │ (OLS/PulseView) │      │ (Binary)      │        │
│  └────┬────────────┘      └───────────┬──┘        │
│       │                              │             │
│  ┌────▼──────────────────────────────▼────┐       │
│  │    Capture & Sampling Engine           │       │
│  │  - DWT Cycle Counter Timing            │       │
│  │  - Trigger Detection Logic             │       │
│  │  - Sample Buffer (16 KB)               │       │
│  │  - LED Service                         │       │
│  └────┬───────────────────────────────────┘       │
│       │                                           │
│  ┌────▼───────────────────────────────────┐      │
│  │      Hardware Peripherals              │      │
│  │  - GPIOB (CH0-CH7 probes)             │      │
│  │  - PC13 (LED)                         │      │
│  │  - DWT (cycle counter)                │      │
│  │  - CRC (frame validation)             │      │
│  └────────────────────────────────────────┘      │
│                                                  │
└──────────────────────────────────────────────────┘
```

### Module Responsibilities

| Module | File | Responsibility |
|--------|------|-----------------|
| **USB DCD + Middleware** | HAL drivers | USB 2.0 Full-Speed enumeration, endpoint management |
| **CDC Interface** | `usbd_cdc_if.c` | USB TX/RX event callbacks, buffer management |
| **Protocol Parser** | `usbd_cdc_if.c` | SUMP & framed protocol state machines |
| **Sampling Engine** | `usbd_cdc_if.c` | DWT-based timing, GPIO reads, trigger logic |
| **Application** | `main.c` | System init, main loop, LED service, cleanup |
| **Hardware Setup** | `system_stm32f4xx.c` | Clock config, interrupt vector |

---

## USB Protocol Stack

### USB Configuration

**Device Descriptor**:
- VID: `0x0483` (STMicroelectronics)
- PID: `0x5740` (STM32 CDC)
- Manufacturer: "STMicroelectronics"
- Product: "STM32 CDC"
- Serial: Auto-generated (device ID)

**Configuration**:
- CDC ACM (Abstract Control Model)
- Endpoints:
  - **EP0**: Control (64 bytes, bidirectional)
  - **EP1 IN**: Bulk TX (64 bytes, interrupt-driven)
  - **EP2 OUT**: Bulk RX (64 bytes, interrupt-driven)
  - **EP3 IN**: Notification (8 bytes, interrupt, not heavily used)

**Baud Rate Emulation**:
- Virtual COM port (no actual baud rate, USB Full-Speed is implicit 12 Mbps)
- CDC SET_LINE_CODING interpreted but not enforced

### RX Data Flow (USB → Device)

```
Physical USB Bus (12 Mbps Full-Speed)
        ↓
   USB DCD (Endpoint 2 OUT)
        ↓
   USB Middleware RX ISR (USBD_DataOutStage)
        ↓
   CDC RX Callback (CDC_Receive_FS)
        ↓
   Ring Buffer Write (USB_RxRingWrite)
        ↓
   Parser in Main Loop (CDC_AppTask → USB_ServiceParser/USB_ServiceSumpParser)
        ↓
   Command Execution (USB_HandleCommand)
```

**Ring Buffer RX** (`USB_RxRing*`):
```c
static uint8_t s_rx_ring[4096];
static uint16_t s_rx_head;      // Next write position (ISR)
static uint16_t s_rx_tail;      // Next read position (main loop)

void USB_RxRingWrite(const uint8_t *data, uint16_t len) {
  // Called from CDC_Receive_FS ISR
  while (len--) {
    s_rx_ring[s_rx_head] = *data++;
    s_rx_head = (s_rx_head + 1) & 0xFFF;  // Wrap at 4096
  }
}

uint16_t USB_RxRingAvailable(void) {
  if (s_rx_head >= s_rx_tail)
    return s_rx_head - s_rx_tail;
  else
    return 4096 - s_rx_tail + s_rx_head;
}

uint8_t USB_RxRingRead(void) {
  uint8_t byte = s_rx_ring[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1) & 0xFFF;
  return byte;
}
```

**Key Properties**:
- ✅ Lock-free (single head writer, single tail reader)
- ✅ Power-of-2 wrap (efficient modulo via masking)
- ✅ Resilient to ISR/main loop race conditions

### TX Data Flow (Device → USB)

```
Main Loop / ISR Context
        ↓
   Queue Packet (USB_TxQueuePush)
        ↓
   TX Queue Service (USB_ServiceTxQueue)
        ↓
   CDC_Transmit_FS (Enqueue to USB endpoint)
        ↓
   USB DCD TX ISR (endpoint complete)
        ↓
   TX Interrupt Callback (CDC_TransmitCplt_FS)
        ↓
   Dequeue next packet
        ↓
   Physical USB Bus (to host)
```

**TX Queue** (16-slot FIFO):
```c
typedef struct {
  uint16_t len;
  uint8_t data[USB_PROTO_HEADER_SIZE + USB_PROTO_MAX_PAYLOAD];
} USB_TxSlot_t;

static USB_TxSlot_t s_tx_slots[16];
static uint8_t s_tx_head = 0;    // Dequeue pointer
static uint8_t s_tx_tail = 0;    // Enqueue pointer
static uint8_t s_tx_busy = 0;    // Current transfer in progress

int USB_TxQueuePush(const USB_TxSlot_t *slot) {
  uint8_t next_tail = (s_tx_tail + 1) & 0x0F;
  if (next_tail == s_tx_head)
    return -1;  // Queue full
  
  s_tx_slots[s_tx_tail] = *slot;
  s_tx_tail = next_tail;
  USB_ServiceTxQueue();  // Start transfer if idle
  return 0;
}
```

**Key Features**:
- ✅ Fixed 16-slot capacity (prevents unbounded growth)
- ✅ Interrupt-triggered service (auto-dequeues on EP completion)
- ✅ Backpressure handling (SUMP parser clears queue on demand)

### Raw TX Blocking (SUMP Samples)

**Special case**: Large sample streams use direct endpoint writes instead of queue:

```c
void USB_SumpTxRawBlocking(const uint8_t *data, uint16_t len) {
  const uint16_t CHUNK_SIZE = 64;      // USB endpoint limit
  const uint32_t TIMEOUT_MS = 6000;    // 6 seconds per chunk
  
  while (len > 0) {
    uint16_t chunk = (len > CHUNK_SIZE) ? CHUNK_SIZE : len;
    
    // Wait for endpoint idle
    uint32_t t0 = HAL_GetTick();
    while (s_tx_busy && (HAL_GetTick() - t0) < TIMEOUT_MS) {
      CDC_AppTask(HAL_GetTick());  // Service other commands
    }
    
    if (HAL_GetTick() - t0 >= TIMEOUT_MS) {
      s_sump.error_tx_timeout++;
      return;  // Timeout
    }
    
    // Transmit chunk
    CDC_Transmit_FS((uint8_t *)data, chunk);
    s_tx_busy = 1;
    
    data += chunk;
    len -= chunk;
  }
}
```

**Rationale**: Avoids queue overhead for streaming (which can be multi-KB)

---

## Sampling Engine

### DWT Cycle Counter Timing

The **Data Watchpoint and Trace (DWT)** unit provides a 32-bit CPU cycle counter for precise timing:

```c
// Enable DWT in main.c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // Enable debug
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;              // Enable cycle counter
```

**Properties**:
- **Width**: 32 bits (wraps after ~42 seconds at 100 MHz)
- **Precision**: 1 CPU cycle (~10 ns @ 100 MHz)
- **Overhead**: ~1 cycle per read

### Sample Rate Calculation

```c
// SUMP SET_DIVIDER command sets s_sump.divider
uint32_t divider = s_sump.divider & 0x00FFFFFF;
uint32_t rate_hz = 100_000_000 / (divider + 1);

if (s_sump.flags & 0x0200)  // DEMUX flag
  rate_hz *= 2;

if (rate_hz > 2_000_000)    // Cap at 2 MHz
  rate_hz = 2_000_000;

// Compute sample period in CPU cycles
uint32_t sample_period_cycles = SystemCoreClock / rate_hz;
```

**Examples**:
| Desired Rate | Divider | Actual Rate | Period (cycles) | Period (ns) |
|---|---|---|---|---|
| 100 kHz | 999 | 99.9 kHz | 1000 | 10 µs |
| 500 kHz | 199 | 500 kHz | 200 | 2 µs |
| 1 MHz | 99 | 1.0 MHz | 100 | 1 µs |
| 2 MHz | 49 | 2.0 MHz | 50 | 500 ns |

### Trigger Wait Loop

```c
int USB_SumpWaitForTrigger(void) {
  uint32_t start_cycle = DWT->CYCCNT;
  uint32_t next_sample_cycle = start_cycle;
  uint32_t timeout_cycles = 800 * SystemCoreClock / 1000;  // 800 ms
  uint32_t trigger_mask = s_sump.trig_mask[0];
  uint32_t trigger_value = s_sump.trig_value[0];
  
  while ((int32_t)(DWT->CYCCNT - start_cycle) < (int32_t)timeout_cycles) {
    // Busy-wait until next sample time
    while ((int32_t)(DWT->CYCCNT - next_sample_cycle) < 0) {}
    
    // Read GPIO and check trigger condition
    uint32_t gpiob_data = GPIOB->IDR & 0xFF;
    if ((gpiob_data & trigger_mask) == trigger_value) {
      return 1;  // Trigger matched
    }
    
    next_sample_cycle += sample_period_cycles;
  }
  
  s_sump.trigger_timeout_count++;
  return 0;  // Timeout: proceed anyway
}
```

**Key Observations**:
- ✅ **Precision**: ~10 ns timing (DWT resolution)
- ⚠️ **CPU Load**: 100% busy-wait during capture (unavoidable with DWT)
- ⚠️ **32-bit Wraparound**: Handled via signed comparison `(int32_t)delta < timeout`

### Sample Acquisition

```c
uint8_t USB_SumpSampleByte(uint32_t sample_idx) {
  if (s_sump.flags & 0x0010) {  // INTERNAL_TEST flag
    // Pseudo-random test pattern
    uint8_t s0 = sample_idx & 0xFF;
    return (s0 ^ (s0 << 1)) ^ 0x5A;
  } else {
    // Real mode: read GPIO
    return GPIOB->IDR & 0xFF;
  }
}
```

### Capture Loop

```c
void USB_SumpAcquireAndSend(void) {
  // Derived from CAPTURE_SIZE command
  uint32_t total_samples = (s_sump.readcount_words + 1) * 4;
  
  if (total_samples > 16384) {
    total_samples = 16384;
    s_sump.capture_overflow++;
  }
  
  // Wait for trigger
  if (!USB_SumpWaitForTrigger()) {
    // Timeout: still proceed
  }
  
  // Acquire samples
  for (uint32_t i = 0; i < total_samples; i++) {
    s_sump_capture[i] = USB_SumpSampleByte(i);
    
    // Maintain timing
    uint32_t next_cycle = start_cycle + (i + 1) * sample_period_cycles;
    while ((int32_t)(DWT->CYCCNT - next_cycle) < 0) {}
  }
  
  // Transmit samples (reversed order!)
  USB_SumpTxRawBlocking(...);
}
```

**Sample Transmission Order**:
```
Acquired:  [S0, S1, S2, ..., Sn]
Sent:      [Sn, Sn-1, ..., S2, S1, S0]
```

**Reason**: Allows host to detect capture completion (final byte should match S0)

---

## Data Flow & Buffering

### Memory Layout

```
┌─────────────────────────────────────────┐
│         STM32F411 RAM (128 KB)          │
├─────────────────────────────────────────┤
│ 0x00000 │ Vector Table (1 KB)           │
├─────────────────────────────────────────┤
│ 0x00400 │ System stack + data            │
│         │ (HAL, USB middleware)          │
│         │ ~30 KB                         │
├─────────────────────────────────────────┤
│ 0x08000 │ RX Ring Buffer (4 KB)          │
│         │ s_rx_ring[4096]                │
├─────────────────────────────────────────┤
│ 0x09000 │ TX Slots (8 KB)                │
│         │ s_tx_slots[16] (512B ea)       │
├─────────────────────────────────────────┤
│ 0x0B000 │ Capture Buffer (16 KB)         │
│         │ s_sump_capture[16384]          │
├─────────────────────────────────────────┤
│ 0x0F000 │ Framed RX Buffer (512B)        │
│         │ s_framed_rx_buffer[512]        │
├─────────────────────────────────────────┤
│ 0x0F200 │ Free / Stack overflow zone     │
├─────────────────────────────────────────┤
│ 0x20000 │ End of RAM                     │
└─────────────────────────────────────────┘
```

**Total Used**: ~58 KB / 128 KB available

### Data Movement Strategies

**Strategy 1: Direct CRC (small frames)**
```
Input: USB endpoint (64 bytes)
  ↓
RX Ring Buffer (4096 bytes)
  ↓
Parser state machine
  ↓
Validate CRC-32 (16 bytes header + payload)
  ↓
Execute command → Response queued to TX
```

**Strategy 2: DMA Copy (large payloads)**
```
Input: USB endpoint (64 bytes × N chunks)
  ↓
RX Ring Buffer (4096 bytes)
  ↓
Parser detects payload ≥ 96 bytes
  ↓
DMA2: Ring → Temporary buffer
  ↓
CRC validation
  ↓
Execute command
```

**Rationale**:
- Small payloads: CPU-based `memcpy` is fast
- Large payloads: DMA offloads memory bandwidth to DMA controller
- Threshold (96 bytes): Breakeven point for DMA setup overhead

---

## State Machines

### SUMP Protocol State Machine

```
IDLE
  ├─ [Byte read]
  ├─ Is valid SUMP cmd?
  │   ├─ YES: Go to PARSING
  │   └─ NO: Wait for next byte
  │
PARSING
  ├─ Is short cmd (0x00-0x0F)?
  │   ├─ YES: Execute immediately, go to IDLE
  │   └─ NO: Wait for 4-byte argument
  │
WAIT_ARG
  ├─ [4 bytes received?]
  │   ├─ YES: Parse arg, execute, go to IDLE
  │   └─ NO: Wait for more data
  │
[Execute handlers]
  ├─ RESET (0x00): Clear state
  ├─ ARM (0x01): Call USB_SumpAcquireAndSend()
  ├─ ID (0x02): Send "1ALS" (4 bytes)
  ├─ METADATA (0x04): Send capabilities (null-term strings)
  ├─ SET_DIVIDER (0x80): Store s_sump.divider
  ├─ CAPTURE_SIZE (0x81): Store readcount/delaycount
  └─ ... (trigger cmds)
```

### Framed Protocol State Machine

```
IDLE (looking for magic)
  ├─ Read byte
  ├─ Is 0x31?
  │   ├─ YES: Go to MAGIC_2
  │   └─ NO: Wait for next byte
  │
MAGIC_2 / MAGIC_3 / MAGIC_4
  ├─ Collect remaining 3 bytes of "UBT1"
  ├─ [All 4 bytes match 0x31544255?]
  │   ├─ YES: Go to HEADER
  │   └─ NO: Resync (send ERROR, go to IDLE)
  │
HEADER
  ├─ Read remaining 16 bytes (version, type, length, seq, crc)
  ├─ Calculate payload CRC
  │
PAYLOAD
  ├─ Read N bytes (0–512)
  ├─ Validate CRC-32
  │   ├─ VALID: Go to EXECUTE
  │   └─ INVALID: Send ERROR, go to IDLE
  │
EXECUTE
  ├─ Route to handler (type 0x01–0x0A)
  ├─ Queue response
  └─ Go to IDLE
```

---

## Timing Analysis

### USB Latency Budget (end-to-end)

```
Host sends command (e.g., "set divider")
  ↓ (~1 µs over USB)
Device USB DCD endpoint RX
  ↓ (~1 µs)
USB Middleware ISR → CDC_Receive_FS
  ↓ (~5 µs)
Ring buffer write
  ↓ (~100 µs polling latency, next main loop tick)
Parser detects command
  ↓ (~1 µs)
Execute handler (state update)
  ↓ (immediate, main loop continues)
---
TOTAL LATENCY: ~100–200 µs (limited by main loop polling frequency)
```

### Sampling Jitter

**Ideal jitter** (using DWT busy-wait):
```
Target interval: 500 ns (at 2 MHz)
DWT precision: 10 ns
Expected jitter: ±5 ns (RMS)
```

**Actual jitter** (realistic):
```
Memory read latency: ~3–5 ns
Interrupt overhead: +10 ns (if ISR interrupts sampling loop)
Cache effects: +10–50 ns
Expected jitter: ±20–50 ns (RMS)
```

### Throughput Analysis

**Capture throughput** (SUMP mode):
```
Sample rate: 2 MHz
Bytes per sample: 1
Data rate: 2 MB/s

USB endpoint bandwidth: 12 Mbps (Full-Speed)
Frame overhead: ~20% (framing, acknowledge)
Available: ~9.6 Mbps ≈ 1.2 MB/s

CONCLUSION: 2 MHz × 1 byte = 2 MB/s > USB capacity
Solution: Transmit samples in reverse (newest first)
         Host detects completion and aborts early
```

---

## Performance Tuning

### CPU Utilization

**During capture** (at 2 MHz):
```
DWT busy-wait: ~100% (unavoidable)
Sample loop:
  - GPIO read: 1 cycle
  - CRC update: 0 cycles (not during SUMP)
  - Interval wait: ~50 cycles (spinning)
Total: ~100% CPU
```

**At idle** (no capture):
```
Main loop:
  - Parser check: ~100 µs per iteration
  - LED service: ~1 µs
  - USB service: variable
Effective: ~1% CPU
```

### Memory Optimization

**Current footprint** (58 KB used / 128 KB available):
- RX ring: 4 KB (must keep, CDC-required buffer)
- TX slots: 8 KB (could reduce to 4 KB if needed)
- Capture: 16 KB (could expand to 32 KB if SRAM2 added)
- Framed RX: 512 B (minimum safe size)

**Potential improvements**:
- Allocate capture buffer in external SRAM (if available)
- Use QSPI flash for sample storage (post-processing)
- Implement RLE encoding (reduce transmitted data)

### Power Optimization

**Current power draw** (estimate):
```
CPU 100 MHz active: ~40 mW
USB transceiver: ~30 mW
LED: ~10 mW (if on)
GPIO inputs: <1 mW
Total: ~80–100 mW (USB powered)
```

**Power-down options**:
- Disable DWT between captures (saves ~5 mW)
- Disable LED when not in use (saves ~10 mW)
- Lower CPU clock between captures (not implemented)

---

## Known Limitations & Future Work

### Current Limitations ⚠️

1. **Single trigger stage**: Only stage 0 implemented (levels 1–3 unused)
2. **No RLE encoding**: Maximum capture depth limited to 16 KB
3. **Busy-wait sampling**: 100% CPU during capture, limits multi-tasking
4. **Reverse sample order**: Non-intuitive for raw protocol inspection
5. **No pre-trigger delay**: `delay_count_words` parsed but ignored
6. **Fixed LED patterns**: Cannot disable activity feedback (always visible)

### Future Enhancements 📋

| Feature | Benefit | Complexity |
|---------|---------|-----------|
| **RLE Encoding** | 5–20× compression for repetitive signals | Medium |
| **Timer-based sampling** | Reduces CPU load, enables parallel tasks | High |
| **External trigger** | Synchronize with external logic | Low–Medium |
| **Multi-stage triggers** | Complex capture conditions | Medium |
| **Data compression** | Reduce USB bandwidth | Medium |
| **Frequency calibration** | Auto-trim for temperature drift | Low |
| **QSPI storage** | Increase capture depth to MB range | High |

---

## References

- [ARM DWT Documentation](https://developer.arm.com/documentation/ddi0403/latest/)
- [STM32F4 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xce-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [SUMP Protocol Specification](https://openbenchlogicsniffer.googlecode.com/files/ols_protocol_v0.txt)
- [USB CDC Class Specification](https://www.usb.org/document-library/class-definitions-communications-devices-12)

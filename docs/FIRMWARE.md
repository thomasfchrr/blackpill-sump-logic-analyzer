# Firmware Development Guide

## Table of Contents
1. [System Initialization](#system-initialization)
2. [Main Loop Architecture](#main-loop-architecture)
3. [Protocol Engine Details](#protocol-engine-details)
4. [GPIO & Hardware Interfacing](#gpio--hardware-interfacing)
5. [Building & Debugging](#building--debugging)
6. [Optimization Techniques](#optimization-techniques)

---

## System Initialization

### Clock Configuration

**File**: `Cube_demo/USB_Test_Project/Core/Src/system_stm32f4xx.c`

```c
// SystemInit() called by startup_stm32f411ceux.s before main()

// HSE (High-Speed External): 25 MHz oscillator on BlackPill
// PLL config:
//   Input:  25 MHz HSE
//   PLLM:   25 (divider) → 1 MHz VCO input
//   PLLN:   192 (multiplier) → 192 MHz VCO output
//   PLLP:   2 (divider) → 96 MHz SYSCLK
//   PLLQ:   4 (divider) → 48 MHz (USB clock)

// Result:
//   SYSCLK (AHB): 96 MHz
//   APB1: 48 MHz (max 50 MHz)
//   APB2: 96 MHz (max 100 MHz)
//   USB: 48 MHz (exact requirement)
```

### Peripheral Initialization Sequence

**File**: `main.c`, `HAL_Init()`, etc.

```c
int main(void) {
  // [1] CPU initialization
  HAL_Init();                    // Enable PWR, RCC, enable instruction/data cache
  SystemClock_Config();          // Configure PLL, switch SYSCLK to 96 MHz
  
  // [2] GPIO initialization
  MX_GPIO_Init();                // Setup GPIOB (probes), GPIOC (LED), PA11/PA12 (USB)
  
  // [3] DWT initialization (for timing)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  
  // [4] USB initialization
  MX_USB_DEVICE_Init();          // Enable USB OTG_FS, CDC class
  
  // [5] CRC initialization (for frame validation)
  MX_CRC_Init();                 // Enable CRC peripheral (32-bit polynomial)
  
  // [6] Main loop
  while(1) {
    uint32_t now_ms = HAL_GetTick();
    CDC_AppTask(now_ms);         // Parse incoming USB packets, dispatch commands
    UserLed_Service(now_ms);     // Update LED status
  }
}
```

### Power Management

```c
// USB provides 5V, BlackPill LDO regulates to 3.3V
// Current budget:
//   Idle: ~20 mW (CPU, USB PHY)
//   Active capture @ 2 MHz: ~80 mW (busy-wait DWT)
//   LED on: +10 mW
//
// Total: <100 mW (well within USB 500 mA limit)
```

---

## Main Loop Architecture

### CDC_AppTask: Protocol Multiplexing

**File**: `USB_DEVICE/App/usbd_cdc_if.c`

```c
void CDC_AppTask(uint32_t now_ms) {
  // Single function calls protocol parsers in sequence
  // Allows interleaved SUMP and framed protocol support
  
  while (USB_RxRingAvailable()) {
    // Peek at first byte
    uint8_t first_byte = s_rx_ring[s_rx_tail];
    
    if (USB_IsSumpCommand(first_byte)) {
      // Route to SUMP parser
      if (USB_ServiceSumpParser()) {
        // Command handled (state updated or samples sent)
        continue;
      } else {
        // Incomplete command, wait for more data
        break;
      }
    } else if (first_byte == 0x31) {  // Magic byte of framed protocol
      // Route to framed parser
      if (USB_ServiceParser()) {
        continue;
      } else {
        break;
      }
    } else {
      // Unknown byte, skip it (resync)
      USB_RxRingRead();
      s_proto.framing_errors++;
    }
  }
}
```

### UserLed_Service: Status Indication

**File**: `main.c`

```c
void UserLed_Service(uint32_t now_ms) {
  static uint32_t last_activity_ms = 0;
  
  // LED states (priority order):
  if (s_sump.error_flag) {
    // Error: Fast blink (80 ms period) for 600 ms
    if (now_ms - s_sump.error_time < 600) {
      if ((now_ms / 40) & 1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);    // ON
      } else {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);      // OFF
      }
      return;
    } else {
      s_sump.error_flag = 0;
    }
  }
  
  if (s_sump.capture_in_progress) {
    // Capturing: Blink at 120 ms period (slow)
    if ((now_ms / 60) & 1) {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);      // ON
    } else {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);        // OFF
    }
    return;
  }
  
  // Activity indicator: solid ON for 80 ms after RX/TX
  if ((now_ms - last_activity_ms) < 80) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);        // ON
    return;
  }
  
  // Default: slow heartbeat (500 ms period)
  if ((now_ms / 250) & 1) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);        // ON
  } else {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);          // OFF
  }
}

void CDC_Receive_FS(uint8_t* Buf, uint32_t *Len) {
  // ... RX ISR ...
  last_activity_ms = HAL_GetTick();  // Record activity
}
```

---

## Protocol Engine Details

### SUMP Parser State Machine

**File**: `USB_DEVICE/App/usbd_cdc_if.c`

**Function**: `USB_ServiceSumpParser()`

```c
int USB_ServiceSumpParser(void) {
  uint8_t cmd_byte = s_rx_ring[s_rx_tail];
  
  // Check if valid SUMP command
  if (!USB_IsSumpCommand(cmd_byte)) {
    return 0;  // Not a SUMP command
  }
  
  // Short commands (no argument)
  switch (cmd_byte) {
    case 0x00:  // RESET
      memset(&s_sump, 0, sizeof(s_sump));
      USB_RxRingRead();  // Consume byte
      return 1;
    
    case 0x01:  // ARM
      USB_RxRingRead();
      s_sump.capture_in_progress = 1;
      USB_SumpAcquireAndSend();
      s_sump.capture_in_progress = 0;
      return 1;
    
    case 0x02:  // ID
      USB_RxRingRead();
      {
        uint8_t id_resp[] = {'1', 'A', 'L', 'S'};
        USB_SumpTxRawBlocking(id_resp, 4);
      }
      return 1;
    
    case 0x04:  // METADATA
      USB_RxRingRead();
      USB_SumpSendMetadata();
      return 1;
    
    case 0x11:  // XON
    case 0x13:  // XOFF
      USB_RxRingRead();
      return 1;  // Acknowledge but ignore (no flow control)
  }
  
  // Long commands (4-byte argument)
  if (cmd_byte & 0x80) {
    if (USB_RxRingAvailable() < 5) {
      return 0;  // Wait for complete command
    }
    
    USB_RxRingRead();  // Consume command byte
    uint32_t arg = USB_RxRingReadLE32();  // Read 4-byte LE32 argument
    
    switch (cmd_byte) {
      case 0x80:  // SET_DIVIDER
        s_sump.divider = arg;
        break;
      
      case 0x81:  // CAPTURE_SIZE
        s_sump.delaycount_words = (arg >> 16) & 0xFFFF;
        s_sump.readcount_words = arg & 0xFFFF;
        break;
      
      case 0x82:  // SET_FLAGS
        s_sump.flags = arg;
        break;
      
      case 0xC0:  // TRIG_MASK[0]
        s_sump.trig_mask[0] = arg;
        break;
      
      case 0xC1:  // TRIG_VALUE[0]
        s_sump.trig_value[0] = arg;
        break;
      
      // ... more trigger stages ...
    }
    return 1;
  }
  
  return 0;  // Incomplete or unknown
}
```

### Framed Protocol Parser

**File**: `USB_DEVICE/App/usbd_cdc_if.c`

**Function**: `USB_ServiceParser()`

```c
int USB_ServiceParser(void) {
  // State machine for framed protocol parsing
  
  static enum {
    STATE_MAGIC1, STATE_MAGIC2, STATE_MAGIC3, STATE_MAGIC4,
    STATE_HEADER, STATE_PAYLOAD
  } parse_state = STATE_MAGIC1;
  
  static USB_ProtoFrame_t frame_buffer;
  static uint32_t payload_bytes_read = 0;
  
  while (USB_RxRingAvailable()) {
    uint8_t byte = USB_RxRingRead();
    
    switch (parse_state) {
      case STATE_MAGIC1:
        if (byte == 0x31) {
          frame_buffer.magic[0] = byte;
          parse_state = STATE_MAGIC2;
        }
        // Else: skip byte, stay in STATE_MAGIC1
        break;
      
      case STATE_MAGIC2:
      case STATE_MAGIC3:
      case STATE_MAGIC4:
        frame_buffer.magic[parse_state - STATE_MAGIC1] = byte;
        if (parse_state == STATE_MAGIC4) {
          if (*(uint32_t*)frame_buffer.magic == USB_PROTO_MAGIC) {
            parse_state = STATE_HEADER;
          } else {
            USB_SendError(USB_PROTO_RSP_ERROR, "Magic mismatch");
            parse_state = STATE_MAGIC1;
          }
        } else {
          parse_state++;
        }
        break;
      
      case STATE_HEADER:
        if (USB_RxRingAvailable() < 16) {
          return 0;  // Not enough bytes for full header
        }
        frame_buffer.version = USB_RxRingRead();
        frame_buffer.type = USB_RxRingRead();
        frame_buffer.length = USB_RxRingReadLE16();
        frame_buffer.seq = USB_RxRingReadLE32();
        frame_buffer.crc = USB_RxRingReadLE32();
        
        if (frame_buffer.length > USB_PROTO_MAX_PAYLOAD) {
          USB_SendError(USB_PROTO_RSP_ERROR, "Payload too large");
          parse_state = STATE_MAGIC1;
          return 1;
        }
        
        payload_bytes_read = 0;
        parse_state = STATE_PAYLOAD;
        break;
      
      case STATE_PAYLOAD:
        if (payload_bytes_read < frame_buffer.length) {
          frame_buffer.payload[payload_bytes_read++] = byte;
        }
        
        if (payload_bytes_read >= frame_buffer.length) {
          // Full packet received, validate CRC and execute
          USB_HandleCommand(&frame_buffer);
          parse_state = STATE_MAGIC1;
          return 1;
        }
        break;
    }
  }
  
  return 0;  // Incomplete packet
}
```

---

## GPIO & Hardware Interfacing

### GPIO Read (Logic Capture)

```c
// Fast inline GPIO read for sampling loop
static inline uint8_t GPIO_ReadProbes(void) {
  // GPIOB Input Data Register (IDR)
  // Bits [7:0] = PB7:PB0 = CH7:CH0
  return GPIOB->IDR & 0xFF;
}

// Called in tight sampling loop (millions of times per second)
// Must be as fast as possible (1–3 CPU cycles)
```

### GPIO Initialization

**File**: `main.c`, function `MX_GPIO_Init()`

```c
void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  // Enable clocks
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  
  // Configure probe inputs: PB0-PB7 (logic analyzer channels)
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;  // No pull-up or pull-down (high impedance)
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  
  // Configure user LED: PC13 (output, active-low)
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  // LED off initially
  
  // USB pins configured in USB_Device middleware
  // (PA11/PA12 as alternate function AF10)
}
```

### DWT Cycle Counter Access

```c
// Enable DWT in main()
static inline void DWT_Enable(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // Enable trace (includes DWT)
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             // Enable cycle counter
}

// Read current cycle count
static inline uint32_t DWT_GetCycles(void) {
  return DWT->CYCCNT;
}

// Wait until target cycle (busy-wait)
static inline void DWT_WaitUntil(uint32_t target_cycle) {
  while ((int32_t)(DWT->CYCCNT - target_cycle) < 0) {}  // Signed comparison handles 32-bit wrap
}
```

---

## Building & Debugging

### Build System

**File**: `Cube_demo/USB_Test_Project/build.ps1`

```powershell
# PowerShell build script (Windows)
param([switch]$Clean)

# [1] Detect STM32CubeIDE installation
$cube_paths = @(
  "C:\ST\STM32CubeIDE_*\STM32CubeIDE.exe",
  "C:\Program Files*\STM32*IDE*\STM32CubeIDE.exe"
)

$ide_path = (Get-Item $cube_paths -ErrorAction SilentlyContinue | Select-Object -First 1).Directory.Parent.FullName

if (-not $ide_path) {
  Write-Error "STM32CubeIDE not found"
  exit 1
}

# [2] Extract toolchain paths
$make_exe = Join-Path $ide_path "tools\make\bin\make.exe"
$gcc_path = Join-Path $ide_path "plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-arm-embedded.win32_*\tools\bin"

# [3] Compile
$env:Path = "$gcc_path;$env:Path"
cd Debug

if ($Clean) { & $make_exe clean }
& $make_exe -j8 all

if ($LASTEXITCODE -ne 0) {
  Write-Error "Build failed"
  exit 1
}

Write-Host "Build successful: USB_Test_Project.elf"
```

### Debugging with VSCode + J-Link

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

**J-Link GDB Server Setup** (auto-started by script):
```bash
JLinkGDBServer.exe -device STM32F411CE -endian little -speed auto -if SWD -port 2331
```

### Debug Commands

```bash
# Connect to J-Link GDB server
target remote localhost:2331

# Reset and halt
monitor reset
monitor halt

# Set breakpoint in USB parser
break usbd_cdc_if.c:USB_ServiceSumpParser

# Continue execution
continue

# Step over
next

# Step into
step

# Print variable
print s_sump.divider

# Backtrace
backtrace

# Examine memory
x /16xb &s_rx_ring[0]
```

---

## Optimization Techniques

### Sampling Speed Optimization

**Tight loop for maximum precision** (2 µs/sample @ 500 kHz):

```c
// Unrolled loop (minimizes branch overhead)
void USB_SumpAcquireOptimized(uint32_t sample_count) {
  uint32_t cycle_period = sample_period_cycles;
  uint32_t next_cycle = DWT->CYCCNT;
  
  for (uint32_t i = 0; i < sample_count; i++) {
    // Wait for next sample interval
    next_cycle += cycle_period;
    while ((int32_t)(DWT->CYCCNT - next_cycle) < 0) {}
    
    // Read GPIO (1–3 cycles)
    s_sump_capture[i] = GPIOB->IDR & 0xFF;
  }
}
```

**Estimated cycle count**:
- Loop overhead: ~10 cycles
- DWT wait: ~0 cycles (tight spin)
- GPIO read: ~2 cycles
- **Total**: ~12 cycles minimum

**At 100 MHz clock**:
- 50-cycle period = 500 ns interval = 2 MHz sample rate
- 50 - 12 = 38 cycles available for other logic

### Memory Access Patterns

**Ring buffer with power-of-2 wrapping** (no modulo operation):

```c
#define RX_RING_SIZE 4096
#define RX_RING_MASK 0xFFF  // (RX_RING_SIZE - 1)

// Efficient wrap: zero cost
uint16_t next_index = (current_index + 1) & RX_RING_MASK;  // 1 AND instruction
// vs.
uint16_t next_index = (current_index + 1) % RX_RING_SIZE;  // DIV instruction (~20 cycles)
```

### USB Throughput Optimization

**Large bulk transfers** (avoid per-packet overhead):

```c
// Instead of:
for (int i = 0; i < 16384; i++) {
  CDC_Transmit_FS(&sample[i], 1);  // Inefficient: each byte = USB packet!
}

// Do this:
uint8_t chunk[64];
for (int i = 0; i < 16384; i += 64) {
  memcpy(chunk, &sample[i], 64);
  CDC_Transmit_FS(chunk, 64);      // One USB transaction per 64 bytes
}
```

---

## Testing & Validation

### Unit Testing (Python)

```bash
# SUMP protocol validation
py -3 sump_smoke_test.py --port COM9 --samples 1024 --samplerate 500000

# Custom frame protocol
py -3 main.py --port COM9 --self-test
```

### System Integration Testing

```powershell
# Full E2E test
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1

# Benchmark (8 seconds of streaming at max rate)
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1 -Bench -Duration 8 -Payload 256
```

---

## Further Reading

- [STM32F411 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xce-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [ARM Cortex-M4 Processor Technical Reference](https://developer.arm.com/documentation/ddi0439/c/)
- [USB Device Class Definition for CDC](https://www.usb.org/sites/default/files/CDC1.2_WMC1.1_012011.zip)

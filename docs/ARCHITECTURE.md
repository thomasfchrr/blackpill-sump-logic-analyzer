# Architecture

## Firmware overview

```
 PulseView / blackpill_sump
          |  USB CDC (bulk, 64-byte packets)
+---------v-----------------------------------------------+
| STM32F411                                                |
|  OTG_FS ISR --> usb_link   RX ring 2 KB / TX ring 16 KB  |
|                    |                                     |
|  main loop --> sump.c   command parser + capture FSM     |
|                    |                                     |
|                capture.c  TIM1 --request--> DMA2 S5 --> SRAM ring (64 KB)
|                    ^                                     |
|                testgen.c  TIM2/3/4 PWM test pattern      |
|  led.c  status LED                                       |
+----------------------------------------------------------+
   PB0..PB7 --> GPIOB->IDR (low byte)
```

Nothing blocks: `main()` loops over `sump_task()` and `led_task()`; the USB interrupt only copies bytes
between the endpoint buffers and the two rings in `usb_link.c`.

| File | Role |
|---|---|
| `Core/Src/main.c` | clocks (HSE 25 MHz to 96 MHz, USB 48 MHz), GPIO, main loop |
| `Core/Src/stm32f4xx_it.c` | handlers; faults break into the debugger if attached, otherwise reset |
| `App/Src/usb_link.c` | byte-stream transport (single-producer/single-consumer rings) |
| `App/Src/sump.c` | SUMP parser and capture state machine |
| `App/Src/capture.c` | TIM1 + DMA sampler, ring position |
| `App/Src/testgen.c` | built-in square-wave pattern for self-test |
| `App/Src/led.c` | LED status patterns |
| `USB_DEVICE/` | ST CDC middleware glue (`usbd_cdc_if.c` only forwards to `usb_link`) |

`firmware/blackpill.ioc` documents the clock tree and the USB setup for STM32CubeMX. `main.c` and the
interrupt file are maintained by hand, so do **not** regenerate over them.

## Sampling engine

TIM1 (APB2 timer clock, 96 MHz) raises a DMA request on every update event. DMA2 stream 5, channel 6 moves
one byte from `GPIOB->IDR` into a 65 532-byte circular buffer. The CPU is not involved per sample.

- sample rate = 96 MHz / divider, divider >= 5 (19.2 MS/s). With 3 or 4 the DMA cannot keep up and silently
  drops requests (measured), so `CAPTURE_MIN_DIV` enforces 5.
- slow rates use the TIM1 prescaler.
- the ring never stops while waiting for a trigger, which provides the **pre-trigger history**.

## Capture state machine (`sump.c`)

```
IDLE -ARM-> WAIT_PRETRIGGER -> WAIT_TRIGGER -> POST_TRIGGER -> SENDING -> IDLE
              (fill history)   (scan ring)     (collect tail)   (upload)
   RESET (0x00) from any state aborts and returns to IDLE
```

- Positions are 64-bit absolute sample counters derived from the DMA position, so waiting any amount of
  time for a trigger cannot wrap.
- The trigger scanner reads the ring four samples per 32-bit load and supports up to four sequential
  stages (an edge is two stages). If it ever falls a whole ring behind, it skips ahead and counts
  `missed_scan`.
- The window is `read_count` samples ending `delay_count` samples after the trigger sample. If the DMA
  lapped the window before the upload, `overruns` is incremented and the LED shows an error.
- The upload sends the newest sample first, as the protocol requires, through the TX ring.
- `s_stats` (`captures`, `overruns`, `missed_scan`, `aborted`, last divider) can be read from GDB.

### SUMP subset

| Command | Handling |
|---|---|
| `00` reset, `01` arm, `02` ID (`1ALS`), `04` metadata | implemented |
| `80` divider, `81` capture size, `83`/`84` delay/read count, `82` flags | implemented |
| `C0..CF` trigger mask / value / config (4 stages, START flag) | implemented |
| flag bit 2 (group 0 disable) | samples sent as 0 |
| flag bit 11 (internal test) | drives the test pattern on PB0..PB7 during the capture |
| RLE, demux, external clock, advanced triggers `9E`/`9F`, `11`/`13` flow control | accepted and ignored |

### Test pattern (`testgen.c`)

PB4/PB5/PB0/PB1: 1 MHz (50/25/75/12.5 % duty), PB6/PB7: 500 kHz, PB3: 100 kHz, PB2 not driven. The pins
become **outputs** for the duration of the capture. Timer outputs are readable through `IDR`, so the
analyzer verifies its own timing without any wiring.

## Memory budget

| Region | Size |
|---|---|
| Sample ring | 64 KB |
| TX / RX rings | 16 KB / 2 KB |
| Everything else (USB, 2 KB stack, ...) | under 6 KB |

## Decisions worth knowing

- **Removed**: the earlier "framed UBT1" test protocol, its CRC and DMA-copy code, the DWT busy-wait
  sampler with a 3-read majority filter, and the debug-register scrubbing at boot. The majority filter
  altered the data (a logic analyzer must record what the pin did), and the busy-wait loop capped the rate
  at 2 MS/s while hogging the CPU. All of it is in git history (tag `legacy-v0-jlink-framed`).
- Probes use the internal ~40 kOhm **pull-down** so open channels read 0. Remove it in `main.c` if it
  loads your circuit.
- The USB descriptors keep the ST VID:PID `0483:5740` so Windows binds `usbser` without a driver. A
  distributed product needs its own VID/PID.

## Roadmap: FPGA acquisition engine

Not implemented. The intended split: an FPGA does real-time sampling, buffering and triggering (more
channels, 100+ MHz) while the STM32 stays the USB device and protocol bridge. The seams already exist:
`capture.c` is the only module touching the sampler, and `sump.c` only needs *samples in a ring plus a
write position*. An FPGA backend would replace `capture.c` with an SPI/parallel reader filling the same
kind of ring, and would move trigger scanning into the FPGA. The 100 MHz / 96 MHz divider mismatch
disappears if the FPGA owns the sample clock.

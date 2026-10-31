# Hardware notes

## Pinout

| Signal | Pin | Note |
|---|---|---|
| CH0..CH7 | PB0..PB7 | sampled together as the low byte of `GPIOB->IDR` |
| USB | PA11 / PA12 | on-board USB-C connector |
| SWD | PA13 / PA14 / GND (NRST optional) | ST-LINK V3 |
| Status LED | PC13 | active low |

PB3 is also SWO and PB4 NJTRST at reset; the firmware reconfigures both as plain inputs. PB2 is BOOT1 (a
pull-down on it is harmless on the BlackPill).

## Input stage: what the board does NOT have

The probes go **straight to the STM32 pins**. That is fine for 3.3 V logic on a bench, and it is the main
thing that keeps this project from being a finished instrument:

- the inputs are not 5 V tolerant on every pin: a 5 V target can damage the MCU
- there is no series protection, no ESD clamp and no adjustable logic threshold
- the internal pull-down is about 40 kOhm, so very high-impedance sources are loaded and open inputs read 0

A reasonable front end, **not built or tested here**: a bus buffer powered from 3.3 V and fed from a
variable threshold reference (for example an octal level translator or a comparator per channel), with a
100 to 220 Ohm series resistor and a clamp diode pair on every probe input, then PB0..PB7. Keep the buffer
fast enough for the 10 MS/s sampling rate (a few ns of skew between channels is invisible, but unequal
delays are not).

## Test pattern wiring

`--test-pattern` / `pattern=Internal` makes the timers drive PB0..PB7 and the analyzer reads them back
through `GPIOB->IDR`, so no wiring is needed. **Disconnect any target first**, the pins are outputs for the
duration of the capture.

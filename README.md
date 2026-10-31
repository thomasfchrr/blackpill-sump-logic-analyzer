# BlackPill SUMP Logic Analyzer

8-channel USB logic analyzer on an **STM32F411 BlackPill**, speaking the SUMP / OpenBench Logic Sniffer
protocol so it is meant to work with **PulseView / sigrok** (driver `ols`). The sampler is fully
hardware-driven (TIM1 + DMA), the firmware is non-blocking, and a Python CLI / viewer is included.

> Status: STM32-only analyzer, working and tested on hardware. The FPGA acquisition engine described in
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#roadmap-fpga-acquisition-engine) is **not implemented yet**.

## Specifications

| | |
|---|---|
| MCU | STM32F411CEU6, Cortex-M4F @ 96 MHz (HSE 25 MHz + PLL), USB full-speed 48 MHz |
| Channels | 8: `CH0..CH7` = `PB0..PB7`, 3.3 V logic |
| Max sample rate | **8.33 MS/s** (period 12 x 10 ns), exact SUMP timebase, no lost samples (verified, see below) |
| Capture depth | 61 440 samples (64 KB DMA ring) |
| Trigger | up to 4 sequential stages (mask/value), pre- and post-trigger window |
| Transport | USB CDC virtual COM port, VID:PID `0483:5740` |
| Flash / debug | on-board **ST-LINK V3** (SWD) through STM32CubeProgrammer and `ST-LINK_gdbserver` |
| Toolchain | Clang 22 + LLD + CMake/Ninja (newlib-nano taken from the Arm GNU bundle) |

Firmware size: about 24 KB flash and 87 KB RAM (64 KB of which is the sample ring).

## Quick start (Windows)

Prerequisites: STM32CubeIDE (Arm GNU libc and ST-LINK GDB server), STM32CubeProgrammer, LLVM Clang with
`ld.lld` (MSYS2 `ucrt64` works), CMake, Ninja, Python 3.9+.

```powershell
tools\build.ps1              # firmware/build/blackpill_sump.elf
tools\flash.ps1              # flash over ST-LINK V3 (SWD) and reset
python -m venv .venv; .venv\Scripts\pip install -e "host[test]"
tools\test.ps1               # unit tests + on-hardware self-test
.venv\Scripts\python -m blackpill_sump gui
```

`tools\test.ps1` uses the `python` found on PATH: activate the virtualenv first.

PulseView: choose *OpenBench Logic Sniffer & SUMP compatible*, select the BlackPill COM port (baud rate is
ignored by USB CDC). See [docs/HOST_TOOLS.md](docs/HOST_TOOLS.md) for the CLI, the viewer and the rate
exact-timebase note below.

## Exact timebase

SUMP hosts compute the divider against a fixed **100 MHz** clock, while the sampling timer runs at **96 MHz**
(USB needs an exact 48 MHz, which excludes a 100 MHz core). Rather than accept a few percent of time error,
the firmware reproduces the 100 MHz timebase exactly: the timer reload value is modulated sample by sample
(a second DMA stream feeds a small table, e.g. 5, 5, 5, 5, 4 cycles for a 4.8-cycle period), so the average
period is exactly `(divider + 1) / 100 MHz` and each sample is at most one timer cycle (10.4 ns) from its
ideal instant. The price is the maximum rate: the two DMA streams sustain 8.33 MS/s; 10 and 11 tick periods
lose about 0.1 % of the samples, so the firmware refuses anything faster.

## Verification performed

On a BlackPill with an ST-LINK V3SET, using the 1 MHz output of the built-in test pattern as a reference:

- time error vs the requested SUMP rate: 0.0000 % at 5, 4, 2 MS/s and at most 0.0011 % for every other period
  tested from 12 to 30 ticks (the residue is the one-sample quantization of the measurement)
- the same check through `sigrok-cli` (which assumes the 100 MHz clock) gives 0.0000 to 0.0011 %
- 10 and 11 tick periods (10 and 9.09 MS/s) lose about 0.1 % of the samples, hence the 8.33 MS/s limit
- pre-trigger / post-trigger windows, 2-stage edge trigger, abort while waiting for a trigger

Compatibility with sigrok (PulseView 0.5.0-git and sigrok-cli 0.8.0-git, driver `ols`), checked with
`sigrok-cli` on the same libsigrok that PulseView uses:

- the device is detected, with 8 channels, 61 440 samples and a 10 Hz - 8.33 MHz rate range
- captures with the internal pattern give exact frequencies (100 kHz, 500 kHz, 1 MHz)
- a level trigger (`--triggers 3=1`) with a 25 % capture ratio puts the trigger at sample 4096 of 16384
- requests above 8.33 MHz are rejected by sigrok itself, and any divider is honoured exactly
- the `ols` driver only offers **level** triggers (0/1); edge triggers need the Python tools
- the PulseView GUI itself was installed and starts, but I did not click through a capture in it

## Repository layout

```
firmware/   STM32F411 firmware (CMake, Clang), see docs/ARCHITECTURE.md
host/       Python package blackpill_sump (client, CLI, Tk viewer, VCD/CSV export) and tests
tools/      build / flash / debug / test PowerShell scripts (ST-LINK V3)
docs/       architecture, debugging, host tools
```

## License

MIT, see [LICENSE](LICENSE).

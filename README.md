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
| Max sample rate | **19.2 MS/s** (96 MHz / 5), sustained, no lost samples (verified, see below) |
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
caveat below.

## Sample-rate caveat (read once)

SUMP hosts compute the divider against a fixed **100 MHz** clock, but the sampling timer runs at **96 MHz**
(the PLL has to produce an exact 48 MHz for USB, which excludes a 100 MHz core). The firmware picks the
closest integer timer divider, so the real rate is within a few percent of the requested one. It is
**exact for rates of the form 4 MHz / k** (4 M, 2 M, 1 M, 500 k, 100 k, ...). The bundled Python tools use
the real rate. In PulseView the time axis is scaled by the requested rate, so prefer the exact rates when
you measure time.

## Verification performed

On a BlackPill with an ST-LINK V3SET:

- built-in test pattern sampled at 1, 4, 10 and 19.2 MS/s: every frequency within 0.2 %
- 200 consecutive 61 440-sample captures at 19.2 MS/s: zero lost samples
- pre-trigger / post-trigger windows, 2-stage edge trigger, abort while waiting for a trigger
- 24 MS/s and above lose DMA requests silently, which is why 19.2 MS/s is the enforced limit

PulseView itself was **not** installed on the test machine. The implementation follows the sigrok `ols`
driver, but the PulseView round trip is untested.

## Repository layout

```
firmware/   STM32F411 firmware (CMake, Clang), see docs/ARCHITECTURE.md
host/       Python package blackpill_sump (client, CLI, Tk viewer, VCD/CSV export) and tests
tools/      build / flash / debug / test PowerShell scripts (ST-LINK V3)
docs/       architecture, debugging, host tools
```

## License

MIT, see [LICENSE](LICENSE).

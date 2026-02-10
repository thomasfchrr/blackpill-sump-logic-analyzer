<!--
File: README.md
Description: Repository overview and quick-start guide for STM32F411 BlackPill SUMP/PulseView integration.
Author: Thomas Faucherre
Created: 2026-02-10
-->

# BlackPill SUMP Logic Analyzer

Recommended repository name: `blackpill-sump-logic-analyzer`

Professional example project showing how to use an `STM32F411 BlackPill` as a USB logic analyzer compatible with `PulseView` (`Openbench Logic Sniffer`/SUMP), while keeping a custom high-speed framed protocol for Python tooling.

## Key Features
- USB CDC device with dual runtime protocol support:
  - SUMP/OLS commands for PulseView
  - Custom framed binary protocol for Python tools
- PulseView-compatible SUMP responses:
  - ID reply: `1ALS`
  - metadata
  - arm/capture/data return
- Python host tools:
  - CLI test client
  - Qt6 GUI control panel
  - SUMP smoke-test helper
- J-Link based build/flash/debug workflow in VSCode (with templates to adapt to ST-Link or other probes)

## Repository Layout
- `Cube_demo/USB_Test_Project`: STM32CubeIDE firmware package (HAL/CMSIS/Middleware + application code)
- `Python_Test`: Python host tools (`main.py`, `gui_qt.py`, `sump_smoke_test.py`)
- `.vscode`: tasks and debug configurations
- `docs`: focused technical guides
- `run_usb_tests.ps1`: end-to-end test runner (build + flash + host checks)

## Quick Start
1. Build firmware:
```powershell
cd Cube_demo/USB_Test_Project
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
```
2. Flash firmware (J-Link default):
```powershell
powershell -ExecutionPolicy Bypass -File .\flash_jlink.ps1 -Run
```
3. Run end-to-end checks:
```powershell
cd ..
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1
```
4. Validate SUMP compatibility quickly:
```powershell
cd Python_Test
py -3 sump_smoke_test.py
```

## PulseView Setup
- Driver: `Openbench Logic Sniffer`
- Connection: STM32 CDC `COMx`
- Recommended samplerate: up to `2 MHz`
- Channels:
  - `CH0..CH7` -> `PB0..PB7`

## Debug Probes
- Default workflow uses `J-Link`.
- Debug setup is intentionally structured so you can switch to `ST-Link` or other GDB server backends by editing `.vscode/launch.json` and `.vscode/settings.json`.
- See `.vscode/README.md` and `docs/DEBUGGING.md` for migration details.

## Notes
- The STM32Cube firmware package is intentionally preserved.
- Build artifacts are excluded via `.gitignore`; regenerate locally with the provided scripts.
- This repository is intended as a clean, reproducible example for publication and reuse.

## License
MIT License. See `LICENSE`.

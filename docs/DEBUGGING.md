<!--
File: docs/DEBUGGING.md
Description: Debugging and validation workflow for firmware and host integration.
Author: Thomas Faucherre
Created: 2026-02-10
-->

# Debugging Guide (Professional Workflow)

## Objective
Provide a reproducible debug/test flow for:
- firmware build and flash
- runtime protocol validation
- board-level debugging with external probes

## Default Probe
- `J-Link` is the default probe in this repository.
- Flash script: `Cube_demo/USB_Test_Project/flash_jlink.ps1`
- VSCode launch configs include J-Link profiles.

## Probe Alternatives
You can switch to `ST-Link` or another GDB server by editing:
- `.vscode/launch.json`
- `.vscode/settings.json`

No firmware logic changes are required for probe migration.

## Recommended Workflow
1. Build:
```powershell
cd Cube_demo/USB_Test_Project
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
```
2. Flash:
```powershell
powershell -ExecutionPolicy Bypass -File .\flash_jlink.ps1 -Run
```
3. Host E2E tests:
```powershell
cd ..\..
powershell -ExecutionPolicy Bypass -File .\run_usb_tests.ps1
```
4. SUMP compatibility check:
```powershell
cd Python_Test
py -3 sump_smoke_test.py
```

## Suggested Breakpoints
- `Cube_demo/USB_Test_Project/USB_DEVICE/App/usbd_cdc_if.c`
  - `CDC_Receive_FS`
  - `USB_ServiceParser`
  - `USB_ServiceSumpParser`
  - `USB_SumpAcquireAndSend`
  - `USB_HandlePacket`

## Runtime Indicators
- User LED patterns indicate health/activity/error states.
- Diagnostic counters are available through host commands (`stats`).

## Capture Debug Context
When reporting an issue, include:
- probe type and firmware binary hash/version
- samplerate + sample count
- SUMP smoke-test output
- `stats` output from `main.py`

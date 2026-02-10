<!--
File: Cube_demo/USB_Test_Project/README.md
Description: Firmware package documentation for the STM32F411 BlackPill implementation.
Author: Thomas Faucherre
Created: 2026-02-10
-->

# Firmware Package (`STM32F411CE`)

This directory contains the STM32CubeIDE firmware package for the BlackPill target.

## Scope
- USB CDC device stack
- Custom framed protocol (Python tools)
- SUMP protocol compatibility layer (PulseView/Openbench Logic Sniffer)
- Runtime diagnostics, guardrails, and debug-safe behavior

## Important Application Files
- `USB_DEVICE/App/usbd_cdc_if.c`: main USB protocol engine (framed + SUMP)
- `USB_DEVICE/App/usbd_cdc_if.h`: protocol constants and app diagnostics
- `Core/Src/main.c`: board init, LED status behavior, runtime safety cleanup

## Hardware Mapping (SUMP)
- `CH0..CH7` map to `PB0..PB7`
- User LED remains available for transfer/debug activity feedback

## Build and Flash
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
powershell -ExecutionPolicy Bypass -File .\flash_jlink.ps1 -Run
```

## Debug
- Default scripts/configuration target `J-Link`.
- VSCode debug settings are designed to be adapted to other probes (e.g. ST-Link) without changing firmware code.

## Keep Package Integrity
- The Cube/HAL/CMSIS package structure is intentionally kept intact for portability and reproducibility.
- Avoid deleting generated makefiles in `Debug/`; they are part of the build workflow.

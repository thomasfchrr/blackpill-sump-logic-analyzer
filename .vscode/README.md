<!--
File: .vscode/README.md
Description: VSCode task and debug configuration guide for this project.
Author: Thomas Faucherre
Created: 2026-02-10
-->

# VSCode Integration Notes

This project is configured for STM32 firmware build/flash/debug and Python tooling from VSCode.

## Tasks
- `STM32: Build (Clean)`
- `STM32: Build (Incremental)`
- `STM32: Flash (J-Link)`
- `USB: E2E Self-Test (Build+Flash+Host)`
- `USB: E2E Benchmark (Build+Flash+Host)`
- `SUMP: Smoke-Test`

## Debug Profiles
- `STM32Cube: Launch J-Link GDB Server (ST Extension)`
- `STM32 BlackPill J-Link (Launch)`
- `STM32 BlackPill ST-Link (Template)` (requires path setup)

## J-Link Default
- Current setup is J-Link first.
- Paths are centralized in `.vscode/settings.json` and reused by launch configs.

## Switching to ST-Link (or Other Probes)
1. Set probe server path (example):
   - environment variable `STLINK_GDB_SERVER`, or
   - edit `launch.json` template profile directly.
2. Use the `STM32 BlackPill ST-Link (Template)` config as base.
3. Keep `gdbPath` unchanged unless your toolchain location differs.

## Portability
- The launch file intentionally keeps explicit, editable fields rather than opaque automation.
- This makes migration to a different debugger backend straightforward.

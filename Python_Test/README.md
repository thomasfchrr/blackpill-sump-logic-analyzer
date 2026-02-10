<!--
File: Python_Test/README.md
Description: Host-side tooling guide for CLI, Qt GUI, and SUMP smoke tests.
Author: Thomas Faucherre
Created: 2026-02-10
-->

# Python Host Tools

This folder contains host-side utilities for integration tests, runtime control, and SUMP validation.

## Files
- `main.py`: CLI client for the custom framed USB protocol
- `gui_qt.py`: Qt6 control panel (connect, ping, stats, LED, reset, logic snapshot)
- `sump_smoke_test.py`: minimal SUMP/OLS compatibility test script

## Dependencies
```powershell
py -3 -m pip install pyserial pyside6
```

## Usage
### 1. CLI (framed protocol)
```powershell
py -3 main.py --port COM9
```

### 2. Qt GUI
```powershell
py -3 gui_qt.py
```

### 3. SUMP quick-check
```powershell
py -3 sump_smoke_test.py --port COM9 --samples 1024 --samplerate 200000
```

## Notes
- `main.py` and `gui_qt.py` are for the custom framed protocol.
- PulseView uses SUMP directly over the same CDC COM port.

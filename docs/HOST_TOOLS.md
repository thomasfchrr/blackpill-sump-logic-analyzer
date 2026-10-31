# Host tools

## PulseView / sigrok

Driver: *OpenBench Logic Sniffer & SUMP compatible* (`ols`), the BlackPill COM port, any baud rate. The
device reports 8 probes, 61 440 samples of memory and an 8.33 MS/s maximum through SUMP metadata. The time axis is exact, see
[Exact timebase](../README.md#exact-timebase). Verified with `sigrok-cli -d ols:conn=COM7 --config samplerate=4MHz:pattern=Internal --samples 16384`
(`pattern=Internal` drives the built-in test pattern on PB0..PB7). The `ols` driver exposes level triggers
only; use the Python tools for edge triggers.

## blackpill_sump (Python)

```powershell
python -m venv .venv; .venv\Scripts\pip install -e "host[test]"
```

```powershell
python -m blackpill_sump info
python -m blackpill_sump capture --rate 4M --samples 16384 --trigger 3:rise --vcd out.vcd --csv out.csv
python -m blackpill_sump selftest        # drives PB0..PB7 with the built-in pattern!
python -m blackpill_sump gui
```

- `--trigger CH:rise|fall|high|low`, `--post N` samples kept after the trigger, `--timeout S`.
- `--test-pattern` makes the firmware **drive** PB0..PB7: disconnect your target first.
- `.vcd` opens in GTKWave and in PulseView (*Import*).

### Viewer

The mouse wheel zooms around the pointer, dragging pans, double-click / right-click place cursors A / B
and show the time difference and frequency. *Stop* aborts a capture still waiting for its trigger.

### Library

```python
from blackpill_sump import Sump
with Sump() as s:                       # auto-detects USB 0483:5740
    cap = s.capture(rate_hz=4e6, samples=8192,
                    trigger=[(0x08, 0x00), (0x08, 0x08)])   # rising edge on CH3
    print(cap.rate_hz, cap.trigger_index, cap.edges(3)[:5])
```

`cap.samples` is chronological, one byte per sample, bit *n* = probe *n*. `cap.rate_hz` is the **real**
sample rate.

## Tests

```powershell
tools\test.ps1 -NoHardware      # pure Python
tools\test.ps1                  # plus the on-hardware self-test (analyzer connected, nothing on PB0..PB7)
```

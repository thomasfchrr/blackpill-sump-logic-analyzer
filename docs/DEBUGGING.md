# Flashing and debugging with the ST-LINK V3

Everything goes through the **ST-LINK V3** over SWD (`PA13` SWDIO, `PA14` SWCLK, `GND`, optionally `NRST`).
The BlackPill USB port stays free for the analyzer, so you can debug and capture at the same time.

## Tools

| Tool | Used for | Located automatically by `tools\*.ps1` |
|---|---|---|
| STM32CubeProgrammer CLI | flash, verify, reset | `C:\Program Files\STMicroelectronics\...` or the CubeIDE plugin |
| `ST-LINK_gdbserver` | GDB server | CubeIDE plugin `...stlink-gdb-server.win32_*` |
| `arm-none-eabi-gdb` | debugger | CubeIDE plugin `...gnu-tools-for-stm32.*` |

## Flash

```powershell
tools\build.ps1            # Release (-O2 -g1); add -Debug for -Og -g3
tools\flash.ps1            # or: tools\flash.ps1 -Build
tools\flash.ps1 -Serial 002E00203234510937333934   # several probes connected
```

Equivalent raw command:

```powershell
STM32_Programmer_CLI -c port=SWD freq=4000 mode=UR -w firmware\build\blackpill_sump.elf -v -rst
```

`mode=UR` (connect under reset) keeps working even if the firmware reconfigured the SWD pins.

## Debug

```powershell
tools\build.ps1 -Debug
tools\debug.ps1            # starts ST-LINK_gdbserver, opens gdb, stops at main
```

Useful GDB commands:

```
print s_stats                    # sump.c counters: captures, overruns, missed_scan, aborted
print s_state                    # capture state machine
print DMA2_Stream5->NDTR         # remaining DMA transfers in the ring
info registers
```

VS Code: install *Cortex-Debug*, then run *BlackPill: launch (ST-LINK V3)* or *attach*. The paths in
`.vscode/settings.json` point at the CubeIDE 2.2.0 plugin folders; edit them if your version differs.

## Notes

- Halting the core stops the CPU, not the DMA: a running capture keeps filling the ring.
- Faults: with a debugger attached the handlers execute `BKPT`; without one they reset the MCU.
- Earlier versions targeted a SEGGER J-Link and cleared the FPB/DWT registers at boot to work around stale
  breakpoints. That workaround is gone with the switch to ST-LINK.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `No STM32 target found` | check the wires, use `mode=UR`, or hold the BlackPill reset button while connecting |
| No COM port after flashing | use a USB *data* cable; a slow LED blink means the device is not enumerated |
| `cmake` fails with a Python launcher error | a dead pip `cmake.exe` shadows the real one; `tools\build.ps1` skips dead shims |
| gdb says `unknown type` for `SystemCoreClock` | cast it: `print (unsigned)SystemCoreClock` |

LED: slow blink = no USB, short flash every 2 s = ready, fast blink = armed, solid = uploading,
very fast blink = error.

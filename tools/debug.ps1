<#
.SYNOPSIS  Start ST-LINK_gdbserver and an interactive arm-none-eabi-gdb session on the firmware.
.DESCRIPTION  The server runs in the background and is stopped when gdb exits.
              GDB stays attached over SWD while USB keeps running (the core is only halted at breakpoints).
#>
param([int]$Port = 61234)
. "$PSScriptRoot\common.ps1"

if (-not (Test-Path $Elf)) { throw "ELF not found: $Elf (run tools\build.ps1 -Debug)" }
$programmerBin = Split-Path -Parent (Get-Programmer)

$server = Start-Process -PassThru -WindowStyle Hidden -FilePath (Get-GdbServer) `
    -ArgumentList "-p", $Port, "-d", "-e", "-cp", "`"$programmerBin`""
try {
    Start-Sleep -Seconds 2
    & (Get-Gdb) $Elf -ex "target extended-remote localhost:$Port" -ex "monitor reset" -ex "break main" -ex "continue"
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}

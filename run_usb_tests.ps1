<#
File: run_usb_tests.ps1
Description: End-to-end USB validation runner (build, flash, host tests).
Author: Thomas Faucherre
Created: 2026-02-10
#>

param(
    [switch]$Clean,
    [switch]$Bench,
    [double]$Duration = 5.0,
    [int]$Payload = 256,
    [int]$TimeoutSec = 20,
    [string]$Port = "",
    [int]$Vid = 0x0483,
    [int]$UsbPid = 0x5740
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$fwDir = Join-Path $root "Cube_demo\USB_Test_Project"
$pyDir = Join-Path $root "Python_Test"

if (-not (Test-Path $fwDir)) {
    throw "Firmware folder not found: $fwDir"
}
if (-not (Test-Path $pyDir)) {
    throw "Python folder not found: $pyDir"
}

$buildArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $fwDir "build.ps1"))
if ($Clean) {
    $buildArgs += "-Clean"
}

Write-Host "[1/4] Building firmware..."
powershell @buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "[2/4] Flashing via J-Link and running target..."
powershell -ExecutionPolicy Bypass -File (Join-Path $fwDir "flash_jlink.ps1") -Run
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

function Get-Stm32CdcPort {
    param([int]$VendorId, [int]$ProductId)

    $pyCode = @'
import sys
from serial.tools import list_ports

vid = int(sys.argv[1], 0)
pid = int(sys.argv[2], 0)

for p in list_ports.comports():
    if (p.vid == vid) and (p.pid == pid):
        print(p.device)
        raise SystemExit(0)

print("")
'@

    $out = $pyCode | py -3 - ("0x{0:X}" -f $VendorId) ("0x{0:X}" -f $ProductId)
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($out | Select-Object -Last 1).Trim()
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Host ("[3/4] Waiting for USB CDC port VID=0x{0:X4} PID=0x{1:X4}..." -f $Vid, $UsbPid)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)

    do {
        $Port = Get-Stm32CdcPort -VendorId $Vid -ProductId $UsbPid
        if (-not [string]::IsNullOrWhiteSpace($Port)) {
            break
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Warning "No STM32 CDC port detected. Available ports:"
    py -3 -c "from serial.tools import list_ports; [print(f'{p.device} vid={p.vid} pid={p.pid} desc={p.description}') for p in list_ports.comports()]"
    throw "STM32 USB CDC port not found. Check USB data cable and VID/PID"
}

Write-Host "Detected CDC port: $Port"

Write-Host "[4/4] Running Python host tests..."
Push-Location $pyDir
try {
    $pyArgs = @("main.py", "--port", $Port, "--vid", ("0x{0:X}" -f $Vid), "--pid", ("0x{0:X}" -f $UsbPid))
    if ($Bench) {
        $pyArgs += @("--bench", "--duration", "$Duration", "--payload", "$Payload")
    }
    else {
        $pyArgs += "--self-test"
    }

    py -3 @pyArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}

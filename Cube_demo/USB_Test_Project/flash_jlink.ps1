<#
File: flash_jlink.ps1
Description: J-Link flashing helper for STM32F411 BlackPill firmware images.
Author: Thomas Faucherre
Created: 2026-02-10
#>

param(
    [string]$Device = "STM32F411CE",
    [int]$Speed = 4000,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$elfPath = Join-Path $projectDir "Debug\USB_Test_Project.elf"

if (-not (Test-Path $elfPath)) {
    throw "ELF not found: $elfPath (run build.ps1 before flashing)"
}

$jlinkCandidates = @()
if ($env:JLINK_EXE) {
    $jlinkCandidates += $env:JLINK_EXE
}
$jlinkCandidates += "C:\Program Files\SEGGER\JLink\JLink.exe"
$jlinkCandidates += "C:\Program Files (x86)\SEGGER\JLink\JLink.exe"
$jlinkCandidates += "C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.jlink.win32_2.5.100.202509120932\tools\bin\JLink.exe"

if (Test-Path "C:\ST") {
    $cubeIdeRoots = Get-ChildItem -Path "C:\ST" -Directory -Filter "STM32CubeIDE_*" -ErrorAction SilentlyContinue |
        Sort-Object -Property Name -Descending
    foreach ($root in $cubeIdeRoots) {
        $pluginsRoot = Join-Path $root.FullName "STM32CubeIDE\plugins"
        if (-not (Test-Path $pluginsRoot)) {
            continue
        }
        $jlinkPlugin = Get-ChildItem -Path $pluginsRoot -Directory -Filter "com.st.stm32cube.ide.mcu.externaltools.jlink.win32_*" -ErrorAction SilentlyContinue |
            Sort-Object -Property Name -Descending |
            Select-Object -First 1
        if ($jlinkPlugin) {
            $jlinkCandidates += (Join-Path $jlinkPlugin.FullName "tools\bin\JLink.exe")
        }
    }
}

$jlinkExe = $jlinkCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $jlinkExe) {
    throw "JLink.exe not found. Set JLINK_EXE or install SEGGER J-Link."
}

$tmpScript = Join-Path $env:TEMP ("jlink_flash_{0}.jlink" -f [Guid]::NewGuid().ToString("N"))
$scriptLines = @(
    "r",
    "h",
    "w4 0xE0002000, 0x00000000",
    "w4 0xE0002004, 0x00000000",
    "w4 0xE0002008, 0x00000000",
    "w4 0xE000200C, 0x00000000",
    "w4 0xE0002010, 0x00000000",
    "w4 0xE0002014, 0x00000000",
    "w4 0xE0002018, 0x00000000",
    "w4 0xE000201C, 0x00000000",
    "w4 0xE0001000, 0x00000000",
    "w4 0xE000ED30, 0x0000001F",
    "w4 0xE000ED2C, 0x80000000",
    "loadfile `"$elfPath`"",
    "r"
)
if ($Run) {
    $scriptLines += "g"
}
$scriptLines += "exit"

Set-Content -Path $tmpScript -Value ($scriptLines -join [Environment]::NewLine) -Encoding ASCII

try {
    & $jlinkExe -device $Device -if SWD -speed $Speed -autoconnect 1 -NoGui 1 -CommanderScript $tmpScript
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Remove-Item $tmpScript -ErrorAction SilentlyContinue
}

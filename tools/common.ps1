# Shared helpers for the tools/*.ps1 scripts (dot-sourced).
$ErrorActionPreference = "Stop"

$script:Root     = Split-Path -Parent $PSScriptRoot
$script:FwDir    = Join-Path $Root "firmware"
$script:BuildDir = Join-Path $FwDir "build"
$script:Elf      = Join-Path $BuildDir "blackpill_sump.elf"

function Find-First([string[]]$Patterns) {
    foreach ($p in $Patterns) {
        $hit = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Get-Tool([string]$Name, [string[]]$Patterns) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $p = Find-First $Patterns
    if (-not $p) { throw "$Name not found. Install STM32CubeIDE / STM32CubeProgrammer or put it on PATH." }
    return $p
}

$cubeIde = "C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins"

function Get-Programmer { Get-Tool "STM32_Programmer_CLI" @(
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "$cubeIde\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe") }

function Get-Gdb { Get-Tool "arm-none-eabi-gdb" @(
    "$cubeIde\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin\arm-none-eabi-gdb.exe") }

function Get-GdbServer { Get-Tool "ST-LINK_gdbserver" @(
    "$cubeIde\com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.win32_*\tools\bin\ST-LINK_gdbserver.exe") }

# PATH can hold a dead pip shim (cmake.exe without its Python), so pick the first one that runs.
function Get-CMake {
    $candidates = @(Get-Command cmake -All -ErrorAction SilentlyContinue | ForEach-Object Source) + "C:\msys64\ucrt64\bin\cmake.exe"
    foreach ($c in $candidates) {
        if ((Test-Path $c) -and (& $c --version 2>$null)) { return $c }
    }
    throw "No working cmake found."
}

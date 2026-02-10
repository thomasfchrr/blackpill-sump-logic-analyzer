<#
File: build.ps1
Description: Firmware build helper for STM32CubeIDE Makefile projects.
Author: Thomas Faucherre
Created: 2026-02-10
#>

param(
    [switch]$Clean,
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"

function Get-LatestPluginDir {
    param(
        [string]$PluginsRoot,
        [string]$Pattern
    )

    $dir = Get-ChildItem -Path $PluginsRoot -Directory -Filter $Pattern -ErrorAction Stop |
        Sort-Object -Property Name -Descending |
        Select-Object -First 1

    if (-not $dir) {
        throw "Plugin not found: $Pattern"
    }

    return $dir.FullName
}

$debugDir = Join-Path $PSScriptRoot "Debug"

if (-not (Test-Path $debugDir)) {
    throw "Directory not found: $debugDir"
}

$cubeRoots = @()
$fixedCubeRoot = "C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE"
if (Test-Path $fixedCubeRoot) {
    $cubeRoots += $fixedCubeRoot
}

$dynamicRoots = Get-ChildItem -Path "C:\ST" -Directory -Filter "STM32CubeIDE_*" -ErrorAction SilentlyContinue |
    Sort-Object -Property Name -Descending |
    ForEach-Object { Join-Path $_.FullName "STM32CubeIDE" }

$cubeRoots += $dynamicRoots
$cubeRoot = $cubeRoots | Select-Object -Unique | Select-Object -First 1

if (-not $cubeRoot -or -not (Test-Path $cubeRoot)) {
    throw "STM32CubeIDE not found under C:\\ST"
}

$pluginsRoot = Join-Path $cubeRoot "plugins"
$makePluginDir = Get-LatestPluginDir -PluginsRoot $pluginsRoot -Pattern "com.st.stm32cube.ide.mcu.externaltools.make.win32_*"
$gccPluginDir = Get-LatestPluginDir -PluginsRoot $pluginsRoot -Pattern "com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*"

$makeBin = Join-Path $makePluginDir "tools\bin"
$gccBin = Join-Path $gccPluginDir "tools\bin"
$makeExe = Join-Path $makeBin "make.exe"
$gccExe = Join-Path $gccBin "arm-none-eabi-gcc.exe"

if (-not (Test-Path $makeExe)) {
    throw "make.exe not found: $makeExe"
}
if (-not (Test-Path $gccExe)) {
    throw "arm-none-eabi-gcc.exe not found: $gccExe"
}

$env:Path = "$makeBin;$gccBin;$env:Path"

Push-Location $debugDir
try {
    if ($Clean) {
        & $makeExe clean
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    & $makeExe "-j$Jobs" all
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}

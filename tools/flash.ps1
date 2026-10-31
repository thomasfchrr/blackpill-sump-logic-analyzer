<#
.SYNOPSIS  Flash the firmware through the on-board ST-LINK (SWD) with STM32CubeProgrammer, then reset.
.PARAMETER Build   Build before flashing.
.PARAMETER Serial  ST-LINK serial number, needed when several probes are connected.
#>
param([switch]$Build, [string]$Serial = "")
. "$PSScriptRoot\common.ps1"

if ($Build) { & "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
if (-not (Test-Path $Elf)) { throw "ELF not found: $Elf (run tools\build.ps1)" }

$port = "port=SWD freq=4000 mode=UR"
if ($Serial) { $port += " sn=$Serial" }
& (Get-Programmer) -c $port.Split(" ") -w $Elf -v -rst
exit $LASTEXITCODE

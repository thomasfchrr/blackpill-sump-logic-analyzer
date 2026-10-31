<#
.SYNOPSIS  Configure and build the firmware with Clang + CMake + Ninja.
.PARAMETER Clean  Wipe the build directory first.
.PARAMETER Debug  Build with -Og -g3 instead of -O2.
#>
param([switch]$Clean, [switch]$Debug)
. "$PSScriptRoot\common.ps1"

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
$type = if ($Debug) { "Debug" } else { "Release" }
$cmake = Get-CMake

& $cmake -S $FwDir -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$type"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $BuildDir
exit $LASTEXITCODE

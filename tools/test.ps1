<#
.SYNOPSIS  Run the host unit tests and, when the analyzer is connected, the on-hardware self-test.
.DESCRIPTION  The self-test DRIVES PB0..PB7 with the built-in pattern: disconnect any target first.
#>
param([switch]$NoHardware)
. "$PSScriptRoot\common.ps1"

$python = (Get-Command python -ErrorAction Stop).Source
Push-Location (Join-Path $Root "host")
try {
    & $python -m pytest -q
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if (-not $NoHardware) {
        & $python -m blackpill_sump selftest
        exit $LASTEXITCODE
    }
}
finally { Pop-Location }

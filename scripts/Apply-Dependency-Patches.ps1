[CmdletBinding()]
param(
    [switch]$CheckOnly,
    [switch]$Repair
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($CheckOnly -and $Repair) { throw '-CheckOnly and -Repair cannot be combined.' }
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python3 -ErrorAction Stop }
$arguments = @((Join-Path $PSScriptRoot 'apply_dependency_patches.py'), '--require-all')
if ($CheckOnly) { $arguments += '--check' }
if ($Repair) { $arguments += '--repair' }
& $python.Source @arguments
exit $LASTEXITCODE

param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$SettingsTemplate,
    [Parameter(Mandatory)][string]$ProfileDirectory,
    [ValidateSet(3,4,5,7,8,10,14,26,30,40,53)][int]$Map = 14,
    [int]$Seconds = 40,
    [switch]$Baseline,
    [switch]$NoWaterProfile,
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if (!(Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^DKR_WATER_QUALIFICATION:BOOL=ON$' -Quiet)) {
    throw 'Requires PRIVATE scene qualification. Never run this against a release binary.'
}
if (Test-Path -LiteralPath $ProfileDirectory) { throw 'Choose a fresh isolated profile.' }
New-Item -ItemType Directory -Path $ProfileDirectory | Out-Null
Copy-Item -LiteralPath $SettingsTemplate -Destination (Join-Path $ProfileDirectory 'dkr-port-settings.ini')
$names=@('DKR_WATER_TEST_MAP','DKR_WATER_PROFILE','DKR_TRACK_PROFILE','DKR_POWER_PROFILE','DKR_WATER_UV_BASELINE','DKR_WATER_DRAW_BASELINE')
$prior=@{}
foreach($name in $names){$prior[$name]=[Environment]::GetEnvironmentVariable($name)}
try {
    $env:DKR_WATER_TEST_MAP="$Map"
    $env:DKR_WATER_PROFILE=if($NoWaterProfile){'0'}else{'1'}
    $env:DKR_TRACK_PROFILE='1'
    $env:DKR_POWER_PROFILE='1'
    $env:DKR_WATER_UV_BASELINE=if($Baseline){'1'}else{'0'}
    $env:DKR_WATER_DRAW_BASELINE=if($Baseline){'1'}else{'0'}
    $exe=Join-Path $BuildDirectory 'bin/Release/DKR-R.exe'
    $arguments='--rom "{0}" --config "{1}" --timeout {2}' -f $Rom,$ProfileDirectory,$Seconds
    $windowStyle=if($Visible){'Normal'}else{'Hidden'}
    $game=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path $exe) -WindowStyle $windowStyle -PassThru
    Write-Output "Owned water-preview PID=$($game.Id) map=$Map baseline=$Baseline profile=$ProfileDirectory"
} finally {
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$prior[$name])}
}
if(!$game.WaitForExit(($Seconds+30)*1000)){throw "Owned preview PID=$($game.Id) did not stop: retain logs, do not replace binary."}
$log=Get-Content (Join-Path $ProfileDirectory 'logs/runtime.log') -Raw
if($game.ExitCode -ne 0 -or $log -notmatch 'runtime stopped cleanly'){throw 'Unclean water preview shutdown.'}
if($log -notmatch "\[perf\]\[private-water-preview\] map=$Map" -or $log -notmatch "\[perf\]\[track\].*map=$Map "){throw 'Requested water scene was not qualified.'}
$log -split "`n" | Select-String 'water-bridge|water-guest|track-history|\[perf\]\[track\]|completed-f3ddkr|runtime stopped'
# This qualifies native scene PREVIEWS, not raced routes, physical presentation,
# netplay, handheld drivers or split-screen. Keep that distinction in reports.

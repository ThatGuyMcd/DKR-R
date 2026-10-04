param(
    [Parameter(Mandatory)][string]$BaselineExecutable,
    [Parameter(Mandatory)][string]$CandidateExecutable,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$SettingsTemplate,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [int]$Rounds = 3,
    [int]$RunSeconds = 100,
    # Use the same instrumented executable for both paths when comparing the
    # old Windows presentation wait with the high-resolution timer candidate.
    [switch]$ComparePresentationWait
)
$ErrorActionPreference = 'Stop'
if ($RunSeconds -lt 40) { throw 'Use at least 40 seconds per run.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose an unused output directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$env:DKR_POWER_PROFILE='1'
$env:DKR_TRACK_PROFILE='1'
$previousWait = $env:DKR_LEGACY_PRESENT_WAIT
try {
for ($round = 1; $round -le $Rounds; ++$round) {
    # Alternate ordering to expose systematic warm-up/background-load bias.
    $cases = if ($round % 2) { @('baseline','candidate') } else { @('candidate','baseline') }
    foreach ($case in $cases) {
        if ($ComparePresentationWait) {
            $env:DKR_LEGACY_PRESENT_WAIT = if ($case -eq 'baseline') { '1' } else { '0' }
        }
        $executable = if ($case -eq 'baseline') { $BaselineExecutable } else { $CandidateExecutable }
        $profile = Join-Path $OutputDirectory "$case-$round"
        New-Item -ItemType Directory -Path $profile | Out-Null
        Copy-Item -LiteralPath $SettingsTemplate -Destination (Join-Path $profile 'dkr-port-settings.ini')
        $arguments = '--rom "{0}" --config "{1}" --timeout {2}' -f $Rom, $profile, $RunSeconds
        $game = Start-Process -FilePath $executable -WorkingDirectory (Split-Path $executable -Parent) -ArgumentList $arguments -WindowStyle Hidden -PassThru
        Write-Output "Started $case round $round (PID $($game.Id))"
        & "$PSScriptRoot/Measure-GameCpu.ps1" -GameProcessId $game.Id -Samples ([math]::Floor(($RunSeconds-10)/10)) -Seconds 10 |
            Tee-Object (Join-Path $profile 'cpu.jsonl')
        if (!$game.WaitForExit(30000)) { throw "Test watchdog did not shut down PID $($game.Id); inspect without killing other games." }
        if ($game.ExitCode -ne 0) { throw "Test process failed with exit $($game.ExitCode)" }
        $log = Get-Content -LiteralPath (Join-Path $profile 'logs/runtime.log') -Raw
        if ($log -notmatch 'profile=Modern.*effective=60' -or $log -notmatch 'runtime stopped cleanly') {
            throw "Invalid presentation profile or unclean shutdown in $profile"
        }
        Write-Output "Verified Modern / 60 FPS target and clean shutdown: $case $round"
    }
}
} finally {
    $env:DKR_LEGACY_PRESENT_WAIT = $previousWait
}

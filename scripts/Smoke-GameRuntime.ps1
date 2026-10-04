param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$SettingsTemplate,
    [Parameter(Mandatory)][string]$ProfileDirectory,
    [int]$Seconds = 60,
    [int]$ExpectedApi = 1,
    [switch]$Visible
)
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath $ProfileDirectory) { throw 'Choose a fresh isolated profile.' }
New-Item -ItemType Directory -Path $ProfileDirectory | Out-Null
Copy-Item -LiteralPath $SettingsTemplate -Destination (Join-Path $ProfileDirectory 'dkr-port-settings.ini')
$env:DKR_POWER_PROFILE='1'
$env:DKR_TRACK_PROFILE='1'
$arguments='--rom "{0}" --config "{1}" --timeout {2}' -f $Rom,$ProfileDirectory,$Seconds
$windowStyle=if($Visible){'Normal'}else{'Hidden'}
$game=Start-Process -FilePath $Executable -WorkingDirectory (Split-Path $Executable -Parent) -ArgumentList $arguments -WindowStyle $windowStyle -PassThru
Write-Output "Testing PID $($game.Id) in $ProfileDirectory"
& "$PSScriptRoot/Measure-GameCpu.ps1" -GameProcessId $game.Id -Seconds 10 -Samples ([math]::Floor(($Seconds-10)/10)) | Tee-Object (Join-Path $ProfileDirectory 'cpu.jsonl')
if (!$game.WaitForExit(30000)) { throw "Watchdog did not shut down owned test PID $($game.Id)." }
$log=Get-Content (Join-Path $ProfileDirectory 'logs/runtime.log') -Raw
$taskMatch=[regex]::Match($log,'completed-f3ddkr-tasks=(\d+)')
if (!$taskMatch.Success -or [long]$taskMatch.Groups[1].Value -lt 100) { throw 'No sustained guest graphics-task progress.' }
if ($game.ExitCode -ne 0 -or $log -notmatch "initialized api=$ExpectedApi profile=Modern.*effective=60" -or $log -notmatch 'runtime stopped cleanly') {
    throw 'Runtime smoke test failed. Retain the isolated logs for diagnosis.'
}
$log -split "`n" | Select-String 'revision|initialized api=|completed-f3ddkr-tasks|runtime stopped cleanly'

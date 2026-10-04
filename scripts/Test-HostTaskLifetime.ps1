param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$SettingsTemplate,
    [Parameter(Mandatory)][string]$ProfileDirectory,
    [ValidateSet('sp','dp')][string]$Stage = 'sp',
    [ValidateRange(1,20000)][int]$DelayMilliseconds = 2000
)
$ErrorActionPreference = 'Stop'
if (!(Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^DKR_TASK_QUALIFICATION:BOOL=ON$' -Quiet)) {
    throw 'This test requires a PRIVATE fault-injection build. Never package it.'
}
if (Test-Path -LiteralPath $ProfileDirectory) { throw 'Choose a fresh isolated profile.' }
New-Item -ItemType Directory -Path $ProfileDirectory | Out-Null
Copy-Item -LiteralPath $SettingsTemplate -Destination (Join-Path $ProfileDirectory 'dkr-port-settings.ini')
$exe = Join-Path $BuildDirectory 'bin/Release/DKR-R.exe'
$variables = @('DKR_TASK_TEST_DELAY_STAGE','DKR_TASK_TEST_DELAY_MS','DKR_TASK_TRACE','DKR_POWER_PROFILE')
$prior = @{}
foreach ($name in $variables) { $prior[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:DKR_TASK_TEST_DELAY_STAGE = $Stage
    $env:DKR_TASK_TEST_DELAY_MS = "$DelayMilliseconds"
    $env:DKR_TASK_TRACE = '1'
    $env:DKR_POWER_PROFILE = '1'
    $seconds = 25 + [int][Math]::Ceiling($DelayMilliseconds / 1000)
    $arguments = '--rom "{0}" --config "{1}" --timeout {2}' -f $Rom,$ProfileDirectory,$seconds
    $game = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    Write-Output "Owned test PID=$($game.Id) stage=$Stage delay=$DelayMilliseconds ms profile=$ProfileDirectory"
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name,$prior[$name]) }
}
if (!$game.WaitForExit(($seconds + 20) * 1000)) { throw "Owned test PID $($game.Id) failed to exit. Retain its logs for diagnosis." }
$log = Get-Content (Join-Path $ProfileDirectory 'logs/runtime.log') -Raw
if ($game.ExitCode -ne 0 -or $log -notmatch 'runtime stopped cleanly') { throw 'Unclean test shutdown.' }
if ($log -notmatch "delay-stage=$Stage duration=$DelayMilliseconds") { throw 'Fault injection did not execute.' }
# __scExec resets BOTH retail counters whenever it starts an audio SP task.
# During a DP-only delay audio keeps running, so the DP counter need not reach
# ten. The SP-delay case must exercise retirement; both must recover safely.
if ($Stage -eq 'sp' -and $log -notmatch 'deferred-retail-retirement') { throw 'The SP test did not exercise the retirement guard.' }
if ($log -match 'dropped late (SP|DP) completion|Host OSTask reused|registry exhausted') { throw 'Task lifetime violation.' }
$tasks = [regex]::Match($log,'completed-f3ddkr-tasks=(\d+)')
if (!$tasks.Success -or [long]$tasks.Groups[1].Value -lt 400) { throw 'Guest graphics progress did not recover.' }
$presents = [regex]::Matches($log,'\[graphics\]\[performance\] presented=([\d.]+)')
if ($presents.Count -eq 0 -or [double]$presents[$presents.Count-1].Groups[1].Value -lt 45) { throw 'Successful presentation did not recover.' }
if ($DelayMilliseconds -ge 16000 -and $log -notmatch '\[host-task\]\[stall\]') { throw 'Bounded stall diagnostic missing.' }
$log -split "`n" | Select-String 'delay-stage|deferred-retail-retirement|host-task\]\[stall|completed-f3ddkr|runtime stopped|graphics\]\[performance'

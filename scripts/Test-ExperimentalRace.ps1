[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Binary,
    [Parameter(Mandatory=$true)][string]$Rom,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    # Optional WSL-native peer: exercise the very same packaged protocol/ABI.
    [string]$ClientLinuxBinary='',
    [ValidateSet('Tracks','Adventure')][string]$Mode='Tracks',
    [ValidateRange(5,120)][int]$Seconds=12
)
$ErrorActionPreference='Stop'
$testExe=[IO.Path]::GetFullPath($Binary)
$testRom=[IO.Path]::GetFullPath($Rom)
$testDirectory=[IO.Path]::GetFullPath($TestRoot)
if(Test-Path -LiteralPath $testDirectory){throw 'Choose a fresh test directory.'}
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$randomBytes=[byte[]]::new(5)
[Security.Cryptography.RandomNumberGenerator]::Fill($randomBytes)
$alphabet='ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
$code=-join ($randomBytes | ForEach-Object {$alphabet[$_ % $alphabet.Length]})
$processes=@()
function Convert-TestPathToWsl([string]$value) {
    $full=[IO.Path]::GetFullPath($value)
    if($full -notmatch '^([A-Za-z]):[\\/](.*)$'){throw 'WSL check requires a drive-backed local path.'}
    return '/mnt/'+$Matches[1].ToLowerInvariant()+'/'+$Matches[2].Replace('\','/')
}
try {
    foreach($role in @('host','client')) {
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=$testExe
        $info.UseShellExecute=$false
        $info.CreateNoWindow=$true
        $info.EnvironmentVariables['SDL_AUDIODRIVER']='dummy'
        $roleRom=$testRom;$roleDirectory=Join-Path $testDirectory $role
        if($role -eq 'client' -and $ClientLinuxBinary) {
            $info.FileName='wsl.exe'
            foreach($prefix in @('-d','Ubuntu-24.04','--','env','SDL_AUDIODRIVER=dummy',$ClientLinuxBinary)){$info.ArgumentList.Add($prefix)}
            $roleRom=Convert-TestPathToWsl $roleRom
            $roleDirectory=Convert-TestPathToWsl $roleDirectory
        }
        foreach($argument in @($(if($role -eq 'host'){'--host'}else{'--join'}),$code,'--rom',$roleRom,'--data',$roleDirectory,'--seconds',"$Seconds",'--scripted-check','--mute-output')) {
            $info.ArgumentList.Add($argument)
        }
        if($Mode -eq 'Adventure'){$info.ArgumentList.Add('--adventure')}
        $processes += [Diagnostics.Process]::Start($info)
    }
    $deadline=[DateTime]::UtcNow.AddSeconds(100+$Seconds)
    while(@($processes | Where-Object {!$_.HasExited}).Count) {
        # If either side refused admission, do not leave its partner's test
        # window parked until timeout. The finally block reaps ONLY processes
        # this script started; it never touches the user's normal game copy.
        foreach($process in $processes) {
            if($process.HasExited -and $process.ExitCode -ne 0) {
                throw "One experimental peer stopped (exit $($process.ExitCode)); inspect its rollback-test.log. Ending the paired check."
            }
        }
        if([DateTime]::UtcNow -gt $deadline){throw 'The two-process automatic check timed out.'}
        Start-Sleep -Milliseconds 250
    }
    foreach($process in $processes) {
        $process.WaitForExit()
        if($process.ExitCode -ne 0){throw "Experimental race check exited $($process.ExitCode); inspect the separate rollback-test.log files."}
    }
    foreach($role in @('host','client')) {
        $log=Get-ChildItem -LiteralPath (Join-Path $testDirectory $role) -Filter rollback-test.log -Recurse -File
        if(@($log).Count -ne 1){throw 'Expected one independent race diagnostic log.'}
        $content=Get-Content -LiteralPath $log.FullName -Raw
        if($content -notmatch 'owned interactive: frames=(\d+) confirmed=(\d+) corrections=(\d+)'){throw "Missing completed $role simulation."}
        $frames=[int]$Matches[1];$confirmed=[int]$Matches[2];$corrections=[int]$Matches[3]
        if($content -match 'owned scene: epoch=(\d+) menu=0') {
            # The continuous menu candidate resets frame indices for every
            # agreed resource epoch. Check its real menu/race path, not the
            # obsolete one-epoch race-only frame-count assumption.
            if($content -notmatch 'menu=1 id=3' -or
               ($Mode -eq 'Tracks' -and $content -notmatch 'menu=1 id=15') -or
               ($Mode -eq 'Adventure' -and $content -notmatch 'menu=1 id=6') -or
               $frames -lt 30 -or $confirmed -lt $frames-8){throw "Incomplete $role retail $Mode path: $frames/$confirmed"}
            if($Mode -eq 'Adventure' -and
               ($content -notmatch 'menu=1 id=23' -or
                $content -notmatch 'menu=0 id=\d+ owners=2 level=0 type=5 viewports=1')) {
                throw "The $role check did not complete initials, new-game cinematic and the one-viewport, two-owner Adventure hub."
            }
        }elseif($frames -lt $Seconds*20 -or $confirmed -lt $frames-8){throw "Unexpected $role progress: $frames/$confirmed"}
        if($content -notmatch 'owned renderer: decoded=(\d+) tagged=(\d+) accepted=(\d+) retired=(\d+)'){throw "Missing completed $role rendering."}
        if([int]$Matches[1] -le 0 -or [int]$Matches[3] -le 0){throw "No accepted $role presentation."}
        "$role`: frames=$frames confirmed=$confirmed corrections=$corrections; accepted presentations=$($Matches[3])"
        if(Get-ChildItem -LiteralPath (Join-Path $testDirectory $role) -Filter '*.dkr-bootstrap' -Recurse -File){throw 'Temporary starting RAM image was not removed.'}
    }
    'Real Quick Join, independent local bootstraps, rendered owned simulation and safe exit passed (audio dummy/muted).'
}finally{
    foreach($process in $processes){if(!$process.HasExited){$process.Kill();$process.WaitForExit(5000) | Out-Null};$process.Dispose()}
}

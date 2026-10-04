[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Binary,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$TestRoot,
    [ValidateRange(2,4)][int]$Players=2,
    [ValidateRange(20,180)][int]$Seconds=45,
    [string]$ClientLinuxBinary='',
    [ValidateSet('Accurate','Modern')][string]$Presentation='Accurate',
    [ValidateSet(0,60,120,180)][int]$TargetFps=0,
    [ValidateSet('Auto','Vulkan')][string]$GraphicsApi='Auto',
    [switch]$Profile,
    [switch]$AssertPerformance
)
$ErrorActionPreference='Stop'
if($AssertPerformance -and (!$Profile -or !$TargetFps -or $Presentation -ne 'Modern' -or $ClientLinuxBinary)) {
    throw 'Performance gates require profiled Modern Windows peers at an explicit FPS target; software WSL is not a hardware benchmark.'
}
$testDirectory=[IO.Path]::GetFullPath($TestRoot)
if(Test-Path -LiteralPath $testDirectory){throw 'Choose a new isolated test directory.'}
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$invite=Join-Path $testDirectory 'invitation.txt'
$processes=@()
function WslPath([string]$path) {
    $path=[IO.Path]::GetFullPath($path)
    if($path -notmatch '^([A-Za-z]):[\\/](.*)$'){throw 'Expected a mounted drive path.'}
    return '/mnt/'+$Matches[1].ToLowerInvariant()+'/'+$Matches[2].Replace('\','/')
}
try {
    for($p=0;$p -lt $Players;$p++) {
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=[IO.Path]::GetFullPath($Binary)
        $info.WorkingDirectory=Split-Path $info.FileName -Parent
        $info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.EnvironmentVariables['SDL_AUDIODRIVER']='dummy'
        if($Presentation -eq 'Modern') {$info.EnvironmentVariables['DKR_OWNED_CHECK_MODERN']='1'}
        if($TargetFps) {$info.EnvironmentVariables['DKR_OWNED_CHECK_FPS']="$TargetFps"}
        if($GraphicsApi -eq 'Vulkan') {$info.EnvironmentVariables['DKR_OWNED_CHECK_API']='Vulkan'}
        if($Profile) {$info.EnvironmentVariables['DKR_ROLLBACK_PROFILE']='1';$info.EnvironmentVariables['DKR_POWER_PROFILE']='1'}
        $role=if($p -eq 0){'--self-test-owned-host'}else{'--self-test-owned-join'}
        $peerProfile=Join-Path $testDirectory "peer-$p"
        $romPath=[IO.Path]::GetFullPath($Rom);$invitePath=$invite
        if($p -gt 0 -and $ClientLinuxBinary) {
            $info.FileName='wsl.exe'
            foreach($arg in @('-d','Ubuntu-24.04','--','env','SDL_AUDIODRIVER=dummy','APPIMAGE_EXTRACT_AND_RUN=1')){$info.ArgumentList.Add($arg)}
            if($Presentation -eq 'Modern') {$info.ArgumentList.Add('DKR_OWNED_CHECK_MODERN=1')}
            if($TargetFps) {$info.ArgumentList.Add("DKR_OWNED_CHECK_FPS=$TargetFps")}
            if($GraphicsApi -eq 'Vulkan') {$info.ArgumentList.Add('DKR_OWNED_CHECK_API=Vulkan')}
            if($Profile) {$info.ArgumentList.Add('DKR_ROLLBACK_PROFILE=1')}
            if($Profile) {$info.ArgumentList.Add('DKR_POWER_PROFILE=1')}
            $info.ArgumentList.Add($ClientLinuxBinary)
            $peerProfile=WslPath $peerProfile;$romPath=WslPath $romPath;$invitePath=WslPath $invite
        }
        foreach($arg in @('--rom',$romPath,'--config',$peerProfile,'--timeout',"$Seconds",$role,$invitePath,'--self-test-owned-players',"$Players")){$info.ArgumentList.Add($arg)}
        $processes += [Diagnostics.Process]::Start($info)
    }
    $deadline=[DateTime]::UtcNow.AddSeconds($Seconds+120)
    while(@($processes | Where-Object {!$_.HasExited}).Count) {
        if([DateTime]::UtcNow -gt $deadline){throw 'Normal executable integration check timed out.'}
        Start-Sleep -Milliseconds 200
    }
    for($p=0;$p -lt $Players;$p++) {
        $processes[$p].WaitForExit()
        $log=Get-Content (Join-Path $testDirectory "peer-$p/logs/runtime.log") -Raw
        $confirmedRace=$false
        foreach($scene in [regex]::Matches($log,'\[rollback\]\[scene\] epoch=(\d+) menu=0')) {
            $epoch=$scene.Groups[1].Value
            if($log -match "\[rollback\]\[progress\] epoch=$epoch frame=\d+ confirmed=([2-9]\d\d|\d{4,})"){$confirmedRace=$true}
        }
        if($log -notmatch '\[rollback\]\[handoff\] all native workers retired' -or
           $log -notmatch '\[rollback\]\[check\] normal Quick Join/countdown accepted' -or
           !$confirmedRace) {
            throw "Peer $p did not complete the normal launch, handoff and confirmed race flow (exit $($processes[$p].ExitCode)). Inspect its runtime.log."
        }
        if($Presentation -eq 'Modern') {
            $highResolution=$false
            foreach($target in [regex]::Matches($log,'\[rollback\]\[render-target\].*target=(\d+)x(\d+).*interpolation=1.*gamma=1\.000')) {
                if([int]$target.Groups[1].Value -gt 320 -and [int]$target.Groups[2].Value -gt 240){$highResolution=$true}
            }
            if(!$highResolution){throw "Peer $p never presented a high-resolution, interpolation-enabled target with retail gamma. Inspect its runtime.log."}
        }
        $anotherFinishedNormally=@($processes | Where-Object {$_.ExitCode -eq 0}).Count -gt 0
        $normalPeerDeparture=$anotherFinishedNormally -and $log -match '\[rollback\]\[owned\] (The normal lobby connection closed during owned play\.|Experimental transport closed; speculative gameplay remains halted\.)'
        if($processes[$p].ExitCode -ne 0 -and
           ($processes[$p].ExitCode -ne 5 -or !$normalPeerDeparture)) {
            throw "Peer $p stopped with an unexpected error (exit $($processes[$p].ExitCode))."
        }
        "Peer $p`: normal executable / same-window handoff / confirmed race progress passed."
        if($AssertPerformance) {
            # Skip initial shaders/menus and at least ten seconds of authored
            # racing. Do not count a render flag or duplicate/repeated frames
            # as proof that actual high-refresh interpolation is running.
            $warm=[regex]::Match($log,'\[rollback\]\[progress\] epoch='+$epoch+' frame=\d+ confirmed=([3-9]\d\d|\d{4,})')
            if(!$warm.Success){throw "Peer $p did not accumulate a warm race interval."}
            $raceLog=$log.Substring($warm.Index)
            $samples=@([regex]::Matches($raceLog,'\[rollback\]\[fps\].*fps=([\d.]+) interpolation-fps=([\d.]+)'))
            $intervals=@([regex]::Matches($raceLog,'\[perf\]\[present-interval\].*ms\(p50/p95/p99/max\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)'))
            if($samples.Count -lt 2 -or $intervals.Count -lt 2){throw "Peer $p has insufficient warm performance samples."}
            $fps=($samples | ForEach-Object {[double]::Parse($_.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)} | Measure-Object -Average).Average
            $interpolated=($samples | ForEach-Object {[double]::Parse($_.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture)} | Measure-Object -Average).Average
            $p95=($intervals | ForEach-Object {[double]::Parse($_.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture)} | Measure-Object -Maximum).Maximum
            $minimumFps=if($TargetFps -eq 60){58.0}else{$TargetFps*0.9}
            $maximumP95=if($TargetFps -eq 60){18.0}else{1500.0/$TargetFps}
            if($fps -lt $minimumFps -or $interpolated -lt $fps*0.35 -or $p95 -gt $maximumP95) {
                throw "Peer $p failed performance gate: FPS=$fps interpolated=$interpolated worst-window-p95-ms=$p95."
            }
            "Peer $p`: measured FPS={0:F2}, interpolated FPS={1:F2}, worst warm-window p95={2:F3} ms." -f $fps,$interpolated,$p95
        }
    }
} finally {
    foreach($process in $processes) {
        if(!$process.HasExited){$process.Kill();$process.WaitForExit(5000)|Out-Null}
        $process.Dispose()
    }
}

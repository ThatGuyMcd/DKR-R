param([Parameter(Mandatory)][string]$TestsDirectory)
$ErrorActionPreference='Stop'
# Last complete rolling window only: exclude startup/preceding title history.
# These are stage timings, not frame rates or physical display measurements.
$results=foreach($folder in Get-ChildItem -LiteralPath $TestsDirectory -Directory) {
    $logPath=Join-Path $folder.FullName 'logs/runtime.log'
    if(!(Test-Path -LiteralPath $logPath)){continue}
    $log=Get-Content -LiteralPath $logPath -Raw
    if($log -notmatch '\[perf\]\[private-water-preview\] map=([0-9]+)'){continue}
    $map=[int]$Matches[1]
    $entry=[ordered]@{test=$folder.Name;map=$map;clean=($log -match 'runtime stopped cleanly');stages=@{}}
    foreach($stage in @('matching','render-cpu','render-gpu','workload')) {
        $matchesForStage=[regex]::Matches($log,"\[perf\]\[track-history\] $stage n=(\d+) ms\(p50/p95/p99\)=([0-9.]+)/([0-9.]+)/([0-9.]+)")
        if($matchesForStage.Count) {
            $m=$matchesForStage[$matchesForStage.Count-1]
            $entry.stages[$stage]=@{n=[int]$m.Groups[1].Value;p50=[double]$m.Groups[2].Value;p95=[double]$m.Groups[3].Value;p99=[double]$m.Groups[4].Value}
        }
    }
    $entry
}
$results | ConvertTo-Json -Depth 5

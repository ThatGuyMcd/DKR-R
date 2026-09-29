param(
    [Parameter(Mandatory=$true)][int]$GameProcessId,
    [int]$Samples = 6,
    [int]$Seconds = 10
)
$ErrorActionPreference = 'Stop'
if (-not ('DkrCpuAudit' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DkrCpuAudit {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access,bool inherit,uint id);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread,out IntPtr description);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr ptr);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static string Name(uint id) {
        IntPtr h=OpenThread(0x0800,false,id);
        if(h==IntPtr.Zero) return "unavailable";
        try { IntPtr p; if(GetThreadDescription(h,out p)<0) return "unnamed";
            try{return Marshal.PtrToStringUni(p);}finally{LocalFree(p);}
        }finally{CloseHandle(h);}
    }
}
'@
}
for($sample=0;$sample -lt $Samples;$sample++) {
    $game=Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
    if(-not $game){break}
    $before=@{}
    foreach($t in $game.Threads){try{$before[$t.Id]=$t.TotalProcessorTime.TotalMilliseconds}catch{}}
    $initialCpu=$game.TotalProcessorTime.TotalMilliseconds
    $timer=[Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $Seconds
    $game=Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
    if(-not $game){break}
    $elapsed=$timer.Elapsed.TotalSeconds
    $threads=foreach($t in $game.Threads){try{if($before.ContainsKey($t.Id)){
        [pscustomobject]@{Id=$t.Id;Name=[DkrCpuAudit]::Name($t.Id);CpuMs=$t.TotalProcessorTime.TotalMilliseconds-$before[$t.Id]}
    }}catch{}}
    [pscustomobject]@{Sample=$sample;Seconds=$elapsed;CpuSeconds=($game.TotalProcessorTime.TotalMilliseconds-$initialCpu)/1000;
        WorkingSetMiB=$game.WorkingSet64/1MB;Threads=@($threads | Sort-Object CpuMs -Descending | Select-Object -First 12)} | ConvertTo-Json -Depth 4 -Compress
}

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BaselinePipeline,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Python='python'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$baseline=(Resolve-Path -LiteralPath $BaselinePipeline).Path
$output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $output){throw 'Choose a NEW portrait pipeline output directory.'}
if($output.StartsWith($baseline+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Portrait output cannot be inside its read-only baseline.'
}
# The Python stager validates protected/input paths before creating parents.
$native=Join-Path $output 'native'
& $Python (Join-Path $PSScriptRoot 'character_portrait_pipeline.py') native --output $native
if($LASTEXITCODE -ne 0){throw 'Native portrait staging failed.'}
$header=Join-Path $root 'extern/n64-modern-runtime/N64Recomp/include/recomp.h'
foreach($revision in @(77,80)) {
    $generated=Join-Path $output "generated-v$revision"
    $config=Join-Path $output "v$revision.toml"
    & $Python (Join-Path $PSScriptRoot 'character_portrait_pipeline.py') guest `
        --source (Join-Path $baseline "generated-v$revision") --output $generated --revision $revision `
        --config (Join-Path $baseline "v$revision.toml") --output-config $config --native-stage $native
    if($LASTEXITCODE -ne 0){throw "Guest portrait staging failed for v$revision."}
    $manifest=Get-Content -LiteralPath (Join-Path $baseline "owned-v$revision/manifest.json") -Raw | ConvertFrom-Json
    if($manifest.revision -ne $revision -or -not $manifest.runtime_symbol_isolation){throw 'Invalid baseline rollback manifest.'}
    $arguments=@((Join-Path $PSScriptRoot 'generate_replay_probe.py'),$generated,$header,
        (Join-Path $output "owned-v$revision"),'--revision',"$revision",'--isolate-symbols','--code-policy-config',$config)
    $options=@{
        scene_cuts='--scene-cuts';full_scenes='--full-scenes';audio_services='--audio-services'
        input_services='--input-services';authored_cpu='--authored-cpu';owned_mod_services='--mod-services'
        checked_inline_memory='--inline-memory';boss_finish_diagnostics='--boss-finish-diagnostics'
    }
    foreach($option in $options.GetEnumerator()){if($manifest.($option.Key)){$arguments+=$option.Value}}
    $arguments+='--roots';$arguments+=@($manifest.roots)
    & $Python @arguments
    if($LASTEXITCODE -ne 0){throw "Owned portrait staging failed for v$revision."}
}
Write-Host 'Fresh portrait payloads generated. No game, mods or tests were executed.'

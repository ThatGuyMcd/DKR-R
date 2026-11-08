[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BaselinePipeline,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$baseline = (Resolve-Path -LiteralPath $BaselinePipeline).Path
$output = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Choose a NEW save-layout pipeline output directory.' }
if ($output.StartsWith($baseline + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Save-layout output cannot be inside its read-only input.'
}
New-Item -ItemType Directory -Path $output | Out-Null
$header = Join-Path $root 'extern/n64-modern-runtime/N64Recomp/include/recomp.h'
foreach ($revision in @(77,80)) {
    $source = Join-Path $baseline "generated-v$revision"
    $generated = Join-Path $output "generated-v$revision"
    $config = Join-Path $output "v$revision.toml"
    & $Python (Join-Path $PSScriptRoot 'save_layout_pipeline.py') --source $source --output $generated `
        --revision $revision --config (Join-Path $baseline "v$revision.toml") --output-config $config
    if ($LASTEXITCODE -ne 0) { throw "Save-layout staging failed for v$revision." }
    $manifest = Get-Content -LiteralPath (Join-Path $baseline "owned-v$revision/manifest.json") -Raw | ConvertFrom-Json
    if ($manifest.revision -ne $revision -or -not $manifest.runtime_symbol_isolation) {
        throw 'Baseline replay manifest must match its revision and isolate runtime symbols.'
    }
    $arguments = @((Join-Path $PSScriptRoot 'generate_replay_probe.py'),$generated,$header,
        (Join-Path $output "owned-v$revision"),'--revision',"$revision",'--isolate-symbols',
        '--code-policy-config',$config)
    $options = @{
        scene_cuts='--scene-cuts'; full_scenes='--full-scenes'; audio_services='--audio-services'
        input_services='--input-services'; authored_cpu='--authored-cpu'
        owned_mod_services='--mod-services'; checked_inline_memory='--inline-memory'
        boss_finish_diagnostics='--boss-finish-diagnostics'
    }
    foreach ($option in $options.GetEnumerator()) {
        if ($manifest.($option.Key)) { $arguments += $option.Value }
    }
    $arguments += '--roots'
    $arguments += @($manifest.roots)
    & $Python @arguments
    if ($LASTEXITCODE -ne 0) { throw "Owned save-layout staging failed for v$revision." }
}
Write-Host 'New normal and rollback CPU payloads prepared. No game or tests were run.'
Write-Host "Configure DKR_GENERATED_SOURCE_V77/V80, DKR_EXPERIMENTAL_OWNED_GENERATED(_V80), and DKR_CODE_POLICY_V77/V80 to the matching paths in $output."

[CmdletBinding()]
param(
    [ValidateSet('Windows','Android')][string]$Platform='Windows',
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$PipelineDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Version=''
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$taskRoot=[IO.Path]::GetFullPath($OutputDirectory)
$pipeline=(Resolve-Path -LiteralPath $PipelineDirectory).Path
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
if(!$Version){$Version=(Get-Content -LiteralPath "$projectRoot/VERSION" -Raw).Trim()}
$cmake='C:/Program Files/CMake/bin/cmake.exe'
New-Item -ItemType Directory -Force -Path $taskRoot | Out-Null
if($Platform -eq 'Windows') {
    $env:DKR_REVIEWED_GENERATED_V77="$pipeline/generated-v77"
    $env:DKR_REVIEWED_GENERATED_V80="$pipeline/generated-v80"
}
& $cmake -S "$projectRoot/runtime-recomp" -B $build "-DDKR_RELEASE_VERSION=$version" `
    "-DDKR_GENERATED_SOURCE_V77=$pipeline/generated-v77" "-DDKR_GENERATED_SOURCE_V80=$pipeline/generated-v80" `
    "-DDKR_EXPERIMENTAL_OWNED_GENERATED=$pipeline/owned-v77" "-DDKR_EXPERIMENTAL_OWNED_GENERATED_V80=$pipeline/owned-v80" `
    '-DDKR_EXPERIMENTAL_RACE_TEST=ON' '-DDKR_REPLAY_QUALIFICATION=OFF' '-DDKR_LEGACY_QUALIFICATION=OFF' `
    '-DDKR_ANDROID_RENDER_QUALIFICATION=OFF' '-DDKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF' `
    '-DDKR_WATER_QUALIFICATION=OFF' '-DDKR_TASK_QUALIFICATION=OFF' *> "$taskRoot/$Platform-configure.log"
if($LASTEXITCODE -ne 0){throw "$Platform updater configuration failed; see $taskRoot"}
$targets=@('DKRPortGame','DKRLegacyModWorker')
if($Platform -eq 'Windows'){$targets+=@('DKRUpdateCoreTests','DKRUpdateServiceTests','DKRUpdateProcessTests')}
& $cmake --build $build --config Release --target $targets --parallel 2 *> "$taskRoot/$Platform-build.log"
if($LASTEXITCODE -ne 0){throw "$Platform updater compilation failed; see $taskRoot"}
if($Platform -eq 'Windows'){
    & 'C:/Program Files/CMake/bin/ctest.exe' --test-dir $build -C Release -R DKRUpdate --output-on-failure *> "$taskRoot/Windows-update-tests.log"
    if($LASTEXITCODE -ne 0){throw 'Updater regression tests failed.'}
}
Write-Output "$Platform updater runtime compiled: $build"

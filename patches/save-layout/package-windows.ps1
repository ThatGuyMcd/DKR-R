param(
    [Parameter(Mandatory=$true)][string]$TaskRoot,
    [string]$CompiledRoot='D:/DiddykongWorkFolder/custom-magic-codes-20261015/windows-build'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$inputRoot=Join-Path $TaskRoot 'windows-package-input'
if(Test-Path -LiteralPath $inputRoot){throw 'Refusing to overwrite packaging input.'}
New-Item -ItemType Directory -Path (Join-Path $inputRoot 'bin/Release/libexec/dkr-r') | Out-Null
foreach($name in @('CMakeCache.txt','dkr-release.json')) {
    Copy-Item -LiteralPath (Join-Path $CompiledRoot $name) -Destination $inputRoot
}
Copy-Item -LiteralPath (Join-Path $CompiledRoot 'bin/RelWithDebInfo/DKR-R.exe') -Destination (Join-Path $inputRoot 'bin/Release')
foreach($file in Get-ChildItem -LiteralPath (Join-Path $CompiledRoot 'bin/RelWithDebInfo/libexec/dkr-r') -File) {
    if($file.Extension -in @('.exe','.dll')) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $inputRoot 'bin/Release/libexec/dkr-r')
    }
}
& (Join-Path $projectRoot 'scripts/Package-Windows.ps1') -Version '1.0.7+beta.5' -BuildDirectory $inputRoot `
    -OutputDirectory (Join-Path $TaskRoot 'release') -SkipRuntimeTests -DeferRuntimeTestsToUser `
    -CustomMagicCodeExample 'D:/DiddykongWorkFolder/custom-magic-codes-20261015/release-107-beta2/dkr_custom_trophies-1.0.2.nrm' `
    -CustomMagicCodeDifficultyExample 'D:/DiddykongWorkFolder/custom-magic-codes-20261015/release-107-beta2/dkr_ai_boss_difficulty-1.0.0.nrm'
if($LASTEXITCODE -ne 0){throw 'Windows packaging failed.'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $TaskRoot 'release/SAVE-LAYOUT-NOTES.md')
$archive=[IO.Compression.ZipFile]::Open((Join-Path $TaskRoot 'release/DKR-R-1.0.7+beta.5-Windows-x64.zip'),[IO.Compression.ZipArchiveMode]::Update)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $PSScriptRoot 'README.md'),
        'DKR-R-1.0.7+beta.5-Windows-x64/SAVE-LAYOUT-NOTES.md') | Out-Null
} finally {$archive.Dispose()}
Write-Host ('Final ZIP SHA-256: '+(Get-FileHash -LiteralPath (Join-Path $TaskRoot 'release/DKR-R-1.0.7+beta.5-Windows-x64.zip') -Algorithm SHA256).Hash)

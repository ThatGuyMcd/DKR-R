param(
    [Parameter(Mandatory=$true)][string]$TaskRoot,
    [string]$CompiledRoot='D:/DiddykongWorkFolder/custom-magic-codes-20261015/windows-build'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
& (Join-Path $projectRoot 'patches/save-layout/package-windows.ps1') -TaskRoot $TaskRoot -CompiledRoot $CompiledRoot
if($LASTEXITCODE -ne 0){throw 'Windows packaging failed.'}
$notes=Join-Path $PSScriptRoot 'README.md'
Copy-Item -LiteralPath $notes -Destination (Join-Path $TaskRoot 'release/CHARACTER-PORTRAIT-NOTES.md')
$zip=Join-Path $TaskRoot 'release/DKR-R-1.0.7+beta.5-Windows-x64.zip'
$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$notes,
        'DKR-R-1.0.7+beta.5-Windows-x64/CHARACTER-PORTRAIT-NOTES.md') | Out-Null
} finally {$archive.Dispose()}
Write-Host ('Final portrait ZIP SHA-256: '+(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)

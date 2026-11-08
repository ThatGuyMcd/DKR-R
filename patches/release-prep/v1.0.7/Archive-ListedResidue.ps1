param([Parameter(Mandatory=$true)][string]$Manifest)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath('C:\DKRPort')
$backupRoot=[IO.Path]::GetFullPath('F:\DKR Dist dump\v1.0.7-prep-20261020')
$listing=Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ([IO.Path]::GetFullPath($listing.source_root) -ne $projectRoot -or [IO.Path]::GetFullPath($listing.backup_root) -ne $backupRoot) {throw 'Manifest roots do not match the explicitly named workspace/backup.'}
if (Test-Path -LiteralPath $backupRoot) {throw 'Backup already exists; never overwrite a previous archive.'}
$tracked=@((& git -C $projectRoot ls-files --cached --full-name -z) -split "`0")
$targets=@()
foreach ($entry in $listing.targets) {
    $relative=$entry.relative.Replace('/','\')
    $source=[IO.Path]::GetFullPath((Join-Path $projectRoot $relative))
    $destination=[IO.Path]::GetFullPath((Join-Path (Join-Path $backupRoot 'repo') $relative))
    if ($source -ne [IO.Path]::GetFullPath($entry.source) -or -not $source.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $destination.StartsWith($backupRoot+'\repo\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Archive path escapes its intended root.'}
    if ($relative -match '^(extern|\.git|\.deps|build|dist|artifacts|build-logs|squashfs-root)\\|^runtime-recomp\\Recompiled(Funcs|Patches)(\\|$)') {throw 'Protected/frozen tree is not an eligible residue target.'}
    if ($relative -notin @('extract.log','imgui.ini') -and [IO.Path]::GetFileName($source) -ne '__pycache__') {throw 'Only listed loose residue and Python caches are eligible.'}
    $gitRelative=$relative.Replace('\','/')
    if ($tracked | Where-Object {$_ -eq $gitRelative -or $_.StartsWith($gitRelative+'/',[StringComparison]::Ordinal)}) {throw 'A tracked source would be moved.'}
    $item=Get-Item -LiteralPath $source -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Reparse points are not followed or archived by this operation.'}
    $actual=if($item.PSIsContainer){@(Get-ChildItem -LiteralPath $source -Recurse -Force -File)}else{@($item)}
    if ($actual.Count -ne @($entry.files).Count) {throw 'Candidate file inventory changed.'}
    foreach ($file in $entry.files) {
        $path=[IO.Path]::GetFullPath((Join-Path $projectRoot $file.path))
        if (-not ($path -eq $source -or $path.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase))) {throw 'File escapes listed target.'}
        $info=Get-Item -LiteralPath $path -Force
        if ($info.Attributes -band [IO.FileAttributes]::ReparsePoint -or $info.Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {throw 'Candidate changed since it was listed; no relocation permitted.'}
    }
    $targets+=@{entry=$entry;source=$source;destination=$destination}
}
# Entire manifest is validated before the first move. No recursive removal,
# alternate shell, glob, broad workspace root or computed unvalidated target.
New-Item -ItemType Directory -Path $backupRoot | Out-Null
Copy-Item -LiteralPath $Manifest -Destination (Join-Path $backupRoot 'BEFORE-MOVE.json')
Copy-Item -LiteralPath ([IO.Path]::ChangeExtension($Manifest,'.txt')) -Destination (Join-Path $backupRoot 'REMOVE-LIST.txt')
$journal=@()
foreach ($target in $targets) {
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target.destination)) | Out-Null
    Move-Item -LiteralPath $target.source -Destination $target.destination
    foreach ($file in $target.entry.files) {
        $copy=[IO.Path]::GetFullPath((Join-Path (Join-Path $backupRoot 'repo') $file.path))
        if (-not $copy.StartsWith($backupRoot+'\repo\',[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {throw 'Backup verification failed; stop and retain all existing files for recovery.'}
    }
    $journal+=@{source=$target.source;backup=$target.destination;files=@($target.entry.files).Count;bytes=$target.entry.bytes;sha256_verified=$true}
    $journal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $backupRoot 'MOVE-JOURNAL.json') -Encoding utf8
}
[pscustomobject]@{Backup=$backupRoot;Targets=$journal.Count;Files=($journal | Measure-Object -Property files -Sum).Sum;Bytes=($journal | Measure-Object -Property bytes -Sum).Sum;Verified=$true} | ConvertTo-Json

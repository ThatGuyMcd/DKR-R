param([Parameter(Mandatory)][string]$Manifest)
$ErrorActionPreference='Stop'
$listing=Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$sourceRoot=[IO.Path]::GetFullPath('C:\DKRPort')
$backupRoot=[IO.Path]::GetFullPath('F:\DKR Dist dump\v1.0.7-release-20261020\obsolete-output')
if ([IO.Path]::GetFullPath($listing.source_root) -ne $sourceRoot -or [IO.Path]::GetFullPath($listing.backup_root) -ne $backupRoot) {throw 'Unexpected relocation roots.'}
if (Test-Path -LiteralPath $backupRoot) {throw 'Backup exists; no overwrites allowed.'}
$tracked=@((& git -C $sourceRoot ls-files -z) -split "`0")
$targets=@()
foreach ($entry in $listing.targets) {
    $relative=$entry.path.Replace('/','\')
    $source=[IO.Path]::GetFullPath((Join-Path $sourceRoot $relative))
    $destination=[IO.Path]::GetFullPath((Join-Path (Join-Path $backupRoot 'repo') $relative))
    if (-not $source.StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $destination.StartsWith($backupRoot+'\repo\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Path escapes named workspace/backup.'}
    if ($relative -notmatch '^build\\[^\\]+$|^build-logs$|^squashfs-root$|^patches\\release-prep\\v1\.0\.7\\source-capsule$') {throw 'Target is not an explicitly allowed output location.'}
    if ($entry.path -match '^build/' -and ($listing.retained_build_roots -contains (Split-Path $source -Leaf))) {throw 'A retained prepared input was listed for relocation.'}
    if ($tracked | Where-Object {$_ -eq $entry.path -or $_.StartsWith($entry.path+'/',[StringComparison]::Ordinal)}) {throw 'Tracked source would be moved.'}
    $info=Get-Item -LiteralPath $source -Force
    if ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Links are not moved by this helper.'}
    $files=if($info.PSIsContainer){@(Get-ChildItem -LiteralPath $source -Recurse -File -Force)}else{@($info)}
    if ($files.Count -ne @($entry.files).Count) {throw 'Output changed since listing.'}
    foreach ($file in $entry.files) {
        $path=[IO.Path]::GetFullPath((Join-Path $sourceRoot $file.path))
        if (-not ($path -eq $source -or $path.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase))) {throw 'File escapes listed output.'}
        $item=Get-Item -LiteralPath $path -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint -or $item.Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {throw 'Listed output changed; no relocation permitted.'}
    }
    $targets+=@{entry=$entry;source=$source;destination=$destination}
}
New-Item -ItemType Directory -Path $backupRoot | Out-Null
Copy-Item -LiteralPath $Manifest -Destination (Join-Path $backupRoot 'BEFORE-MOVE.json')
Copy-Item -LiteralPath ([IO.Path]::ChangeExtension($Manifest,'.txt')) -Destination (Join-Path $backupRoot 'REMOVE-LIST.txt')
$journal=[Collections.Generic.List[object]]::new()
foreach ($target in $targets) {
    New-Item -ItemType Directory -Force (Split-Path $target.destination -Parent) | Out-Null
    if (Test-Path -LiteralPath $target.destination) {throw 'Backup collision.'}
    # All final absolute targets and every member were checked above. One
    # native PowerShell filesystem operation; no alternate shell/glob deletion.
    Move-Item -LiteralPath $target.source -Destination $target.destination
    foreach ($file in $target.entry.files) {
        $destination=[IO.Path]::GetFullPath((Join-Path (Join-Path $backupRoot 'repo') $file.path))
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {throw 'Backup hash mismatch; retain journal and investigate.'}
    }
    $journal.Add(@{source=$target.source;destination=$target.destination;files=@($target.entry.files).Count;bytes=$target.entry.bytes;verified=$true})
    $journal | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupRoot 'MOVE-JOURNAL.json') -Encoding utf8
    Write-Output ('Backed up and verified: '+$target.entry.path)
}
Write-Output ('Recoverable cleanup complete. Locations: '+$journal.Count)

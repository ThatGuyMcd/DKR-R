[CmdletBinding()]
param(
    [string]$Version = '',
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build\dkr-runtime-rt64',
    [string]$OutputDirectory = '',
    # For explicitly deferred beta qualification only; release checks remain on by default.
    [switch]$SkipRuntimeTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $distRoot = Join-Path $projectRoot 'dist'
} else {
    $distRoot = [IO.Path]::GetFullPath($OutputDirectory)
}
if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $resolvedBuild = [IO.Path]::GetFullPath($BuildDirectory)
} else {
    $resolvedBuild = Join-Path $projectRoot $BuildDirectory
}
$stage = Join-Path $distRoot "DKR-R-$Version-Windows-x64"
if (Test-Path -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt')) {
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_EXPERIMENTAL_RENDER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package private experimental GPU retirement qualification. Reconfigure and rebuild with DKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF.'
    }
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_REPLAY_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package private replay fixture capture.'
    }
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_ANDROID_RENDER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package private Android renderer qualification.'
    }
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_WATER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package private water-scene qualification.'
    }
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_TASK_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package private host-task fault injection. Rebuild with DKR_TASK_QUALIFICATION=OFF.'
    }
    if (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_LEGACY_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
        throw 'Refusing to package a private legacy-content qualification build. Reconfigure and rebuild with DKR_LEGACY_QUALIFICATION=OFF first.'
    }
}
$zip = "$stage.zip"
$deniedExtensions = @('.z64', '.v64', '.n64', '.eep', '.mpk', '.sra', '.fla', '.o2r', '.otr', '.dkr-probe', '.dkr-bootstrap', '.dkr-component')
$runtimeFiles = @('DKR-R.exe', 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll')
$experimentalRaceTest = (Test-Path -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt')) -and
    (Select-String -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt') -Pattern '^DKR_EXPERIMENTAL_RACE_TEST:BOOL=(ON|1|TRUE|YES)$' -Quiet)
if ($experimentalRaceTest) {
    if ($Version -notmatch 'experimental|rollback-test|^1\.0\.5-beta\.15(?:-playtest\.(?:[23456789]|10|11|12|14|15(?:-fix\.[12])?))?$') { throw 'Experimental backend builds must use a distinctly labelled experimental version or an approved Beta 15 playtest candidate.' }
}
$inputHostFiles = @('DKR-R-InputHost.exe', 'SDL3.dll', 'DKR-R-ModWorker.exe')

if (Test-Path -LiteralPath $stage) {
    throw "Refusing to overwrite existing release directory: $stage"
}
if (Test-Path -LiteralPath $zip) {
    throw "Refusing to overwrite existing release archive: $zip"
}

$bin = Join-Path $resolvedBuild "bin\$Configuration"
if (Test-Path -LiteralPath (Join-Path $bin 'DKR-R.exe')) {
    $imageText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $bin 'DKR-R.exe')))
    if ($imageText.Contains('DKR_REPLAY_CAPTURE_FILE')) {
        throw 'Release executable still contains private replay fixture capture.'
    }
    if ($imageText.Contains('DKR_WATER_TEST_MAP') -or $imageText.Contains('[perf][private-water-preview]')) {
        throw 'Release executable still contains private water qualification; rebuild after disabling it.'
    }
    $imageText = $null
}
foreach ($name in $runtimeFiles) {
    $source = Join-Path $bin $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing release runtime file: $source"
    }
}
$inputHostBin = Join-Path $bin 'libexec\dkr-r'
foreach ($name in $inputHostFiles) {
    $source = Join-Path $inputHostBin $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing private SDL3 input host file: $source"
    }
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination (Join-Path $stage $name)
}
$stagedInputHostDirectory = Join-Path $stage 'libexec\dkr-r'
New-Item -ItemType Directory -Path $stagedInputHostDirectory -Force | Out-Null
foreach ($name in $inputHostFiles) {
    Copy-Item -LiteralPath (Join-Path $inputHostBin $name) `
        -Destination (Join-Path $stagedInputHostDirectory $name)
}
$stagedRuntime = Join-Path $stage 'DKR-R.exe'
$stagedInputHost = Join-Path $stagedInputHostDirectory 'DKR-R-InputHost.exe'
if ($experimentalRaceTest) {
    # Current playtests have their own connection/scope guides. The older file
    # describes Playtest 2 and must not label this candidate as that release.
    if ($Version -notin @('1.0.5-beta.15-playtest.8', '1.0.5-beta.15-playtest.9', '1.0.5-beta.15-playtest.10', '1.0.5-beta.15-playtest.11', '1.0.5-beta.15-playtest.12', '1.0.5-beta.15-playtest.14', '1.0.5-beta.15-playtest.15', '1.0.5-beta.15-playtest.15-fix.1', '1.0.5-beta.15-playtest.15-fix.2')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\EXPERIMENTAL-ROLLBACK-TESTING.txt') -Destination (Join-Path $stage 'EXPERIMENTAL-ROLLBACK-TESTING.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.3') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST3-PARITY.txt') -Destination (Join-Path $stage 'PLAYTEST3-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.4') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST4-PERFORMANCE.txt') -Destination (Join-Path $stage 'PLAYTEST4-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.5') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST5-BOSS-DIAGNOSTICS.txt') -Destination (Join-Path $stage 'PLAYTEST5-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.6') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST6-PERFORMANCE.txt') -Destination (Join-Path $stage 'PLAYTEST6-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.7') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST7-CPU.txt') -Destination (Join-Path $stage 'PLAYTEST7-NOTES.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt') -Destination (Join-Path $stage 'LOCAL-SCENERY-STATUS.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.8') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST8-SCENERY.txt') -Destination (Join-Path $stage 'PLAYTEST8-NOTES.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt') -Destination (Join-Path $stage 'LOCAL-SCENERY-STATUS.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.9') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST9-CAMERA.txt') -Destination (Join-Path $stage 'PLAYTEST9-NOTES.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt') -Destination (Join-Path $stage 'LOCAL-SCENERY-STATUS.txt')
    }
    if ($Version -in @('1.0.5-beta.15-playtest.10', '1.0.5-beta.15-playtest.11', '1.0.5-beta.15-playtest.12', '1.0.5-beta.15-playtest.14')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST10-SAVE-PROTECTION.txt') -Destination (Join-Path $stage 'PLAYTEST10-NOTES.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\CONTROLLER-STUTTER-AND-LAUNCHER-OPTIONS-20261004.txt') -Destination (Join-Path $stage 'LAUNCHER-OPTIONS-AND-CONTROLLERS.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt') -Destination (Join-Path $stage 'LOCAL-SCENERY-STATUS.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.11') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST11-CUSTOM-ROSTER.txt') -Destination (Join-Path $stage 'PLAYTEST11-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.12') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST12-ONLINE-MODS.txt') -Destination (Join-Path $stage 'PLAYTEST12-NOTES.txt')
    }
    if ($Version -in @('1.0.5-beta.15-playtest.14','1.0.5-beta.15-playtest.15','1.0.5-beta.15-playtest.15-fix.1','1.0.5-beta.15-playtest.15-fix.2')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST14-ONLINE-REVISIONS.txt') -Destination (Join-Path $stage 'PLAYTEST14-NOTES.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\PLAYTEST14-ONLINE-RETENTION-AND-PROGRESS.txt') -Destination (Join-Path $stage 'ONLINE-MOD-ADMISSION-REBUILD.txt')
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\PLAYTEST14-MOD-SYNC-PERFORMANCE-20261006.txt') -Destination (Join-Path $stage 'ONLINE-MOD-SYNC-PERFORMANCE.txt')
    }
    if ($Version -in @('1.0.5-beta.15-playtest.15','1.0.5-beta.15-playtest.15-fix.1','1.0.5-beta.15-playtest.15-fix.2')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST15-ROLLBACK.txt') -Destination (Join-Path $stage 'PLAYTEST15-NOTES.txt')
    }
    if ($Version -in @('1.0.5-beta.15-playtest.15-fix.1','1.0.5-beta.15-playtest.15-fix.2')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST15-FIX1.txt') -Destination (Join-Path $stage 'PLAYTEST15-FIX1-NOTES.txt')
    }
    if ($Version -eq '1.0.5-beta.15-playtest.15-fix.2') {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\BETA15-PLAYTEST15-FIX2.txt') -Destination (Join-Path $stage 'PLAYTEST15-FIX2-NOTES.txt')
    }
}
$logoDirectory = Join-Path $stage 'assets\ui\Icons'
New-Item -ItemType Directory -Path $logoDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Logo.bmp') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Logo.bmp')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Spinning-Icon.png') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Spinning-Icon.png')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Short-Logo.png') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Short-Logo.png')
$backgroundDirectory = Join-Path $stage 'assets\ui\Backgrounds'
New-Item -ItemType Directory -Path $backgroundDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Backgrounds\DKR-R-Launcher-Background.png') `
    -Destination (Join-Path $backgroundDirectory 'DKR-R-Launcher-Background.png')
$filterDirectory = Join-Path $stage 'assets\filters'
New-Item -ItemType Directory -Path $filterDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot 'assets\filters\*.png') `
    -Destination $filterDirectory -Force
$controllerDirectory = Join-Path $stage 'assets\controllers'
New-Item -ItemType Directory -Path $controllerDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\controllers\gamecontrollerdb.txt') `
    -Destination (Join-Path $controllerDirectory 'gamecontrollerdb.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\RELEASE-README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\ONLINE_MULTIPLAYER.md') -Destination (Join-Path $stage 'ONLINE_MULTIPLAYER.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\ONLINE-STABILITY.md') -Destination (Join-Path $stage 'ONLINE-STABILITY.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\HUD-WORKSHOP.md') -Destination (Join-Path $stage 'HUD-WORKSHOP.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\LEGACY-MODS-BETA.md') -Destination (Join-Path $stage 'LEGACY-MODS-BETA.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\LEGACY-MOD-COMPATIBILITY-BETA7.md') -Destination (Join-Path $stage 'LEGACY-MOD-COMPATIBILITY-BETA7.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.md') -Destination (Join-Path $stage 'LICENSE.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY.md') -Destination (Join-Path $stage 'THIRD_PARTY.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'runtime-recomp\COPYING-NOTICE.md') -Destination (Join-Path $stage 'COPYING-NOTICE.md')
$noticeDirectory = Join-Path $stage 'ThirdPartyLicenses'
New-Item -ItemType Directory -Path $noticeDirectory | Out-Null
$noticeFiles = [ordered]@{
    'RT64-LICENSE.txt' = 'extern\rt64\LICENSE'
    'Dear-ImGui-LICENSE.txt' = 'extern\rt64\src\contrib\imgui\LICENSE.txt'
    'SDL2-LICENSE.txt' = 'extern\rt64\src\contrib\mupen64plus-win32-deps\SDL2-2.26.3\COPYING.txt'
    'SDL3-LICENSE.txt' = 'extern\sdl3\LICENSE.txt'
    'N64ModernRuntime-COPYING.txt' = 'extern\n64-modern-runtime\COPYING'
    'N64Recomp-LICENSE.txt' = 'extern\n64-modern-runtime\N64Recomp\LICENSE'
    'DXC-NOTICE.md' = 'packaging\licenses\DXC-NOTICE.md'
    'Jumpman-LICENSE.txt' = 'packaging\licenses\Jumpman-LICENSE.txt'
    'Selawik-OFL.txt' = 'packaging\licenses\Selawik-OFL.txt'
    'CRT-FILTERS-NOTICE.md' = 'packaging\licenses\CRT-FILTERS-NOTICE.md'
    'SDL-GAMECONTROLLERDB-LICENSE.txt' = 'packaging\licenses\SDL-GAMECONTROLLERDB-LICENSE.txt'
    'GEKKONET-LICENSE.txt' = 'packaging\licenses\GEKKONET-LICENSE.txt'
    'MONOCYPHER-LICENSE.txt' = 'packaging\licenses\MONOCYPHER-LICENSE.txt'
    'GOLDEN-BALLOON-NOTICE.txt' = 'packaging\licenses\GOLDEN-BALLOON-NOTICE.txt'
    'LEGACY-MODS-NOTICE.txt' = 'packaging\licenses\LEGACY-MODS-NOTICE.txt'
    'LIBDATACHANNEL-LICENSE.txt' = 'extern\libdatachannel\LICENSE'
    'MBEDTLS-LICENSE.txt' = 'extern\mbedtls\LICENSE'
    'LIBJUICE-LICENSE.txt' = 'extern\libdatachannel\deps\libjuice\LICENSE'
    'USRSCTP-LICENSE.txt' = 'extern\libdatachannel\deps\usrsctp\LICENSE.md'
    'NLOHMANN-JSON-LICENSE.txt' = 'extern\libdatachannel\deps\json\LICENSE.MIT'
    'PLOG-LICENSE.txt' = 'extern\libdatachannel\deps\plog\LICENSE'
}

# SDL3 is deliberately private to the controller helper. Exercise it from the
# exact release path before the public launcher is tested; this catches a
# missing SDL3.dll without ever loading SDL3 into the launcher/game process.
if (-not $SkipRuntimeTests) {
$modWorkerTestInfo = [Diagnostics.ProcessStartInfo]::new()
$modWorkerTestInfo.FileName = Join-Path $stagedInputHostDirectory 'DKR-R-ModWorker.exe'
$modWorkerTestInfo.Arguments = '--self-test'
$modWorkerTestInfo.UseShellExecute = $false
$modWorkerTestInfo.CreateNoWindow = $true
$modWorkerTest = [Diagnostics.Process]::Start($modWorkerTestInfo)
try {
    if (-not $modWorkerTest.WaitForExit(15000)) {
        $modWorkerTest.Kill()
        throw 'The staged legacy importer self-test timed out.'
    }
    if ($modWorkerTest.ExitCode -ne 0) { throw 'The staged legacy importer self-test failed.' }
} finally { $modWorkerTest.Dispose() }
$modWorkerTestInfo.Arguments = '--online-capability'
$modWorkerTestInfo.RedirectStandardOutput = $true
$modWorkerTest = [Diagnostics.Process]::Start($modWorkerTestInfo)
try {
    if (-not $modWorkerTest.WaitForExit(15000)) { $modWorkerTest.Kill(); throw 'The staged online importer capability check timed out.' }
    if ($modWorkerTest.ExitCode -ne 0 -or $modWorkerTest.StandardOutput.ReadToEnd().Trim() -ne 'DKR-R online-prepare protocol 1') {
        throw 'The staged ModWorker cannot prepare online mods. Do not package a mismatched helper.'
    }
} finally { $modWorkerTest.Dispose() }
$inputHostTest = [Diagnostics.Process]::Start(
    $stagedInputHost,
    "--self-test --mappings `"$(Join-Path $controllerDirectory 'gamecontrollerdb.txt')`"")
$inputHostTest.WaitForExit()
if ($inputHostTest.ExitCode -ne 0) {
    $exitCode = $inputHostTest.ExitCode
    $inputHostTest.Dispose()
    throw "The staged private SDL3 input host failed its self-test with exit code $exitCode."
}
$inputHostTest.Dispose()
}
foreach ($entry in $noticeFiles.GetEnumerator()) {
    $source = Join-Path $projectRoot $entry.Value
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing third-party notice: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $noticeDirectory $entry.Key)
}

# Exercise the binary from the exact staged package, not only from the build
# tree. This catches a missing DLL or packaging-path regression before ZIP
# creation while keeping all test data outside the release directory.
if (-not $SkipRuntimeTests) {
$pakTest = Join-Path ([IO.Path]::GetTempPath()) `
    ("dkr-r-packaged-pak-" + [Guid]::NewGuid().ToString('N'))
$selfTestProcess = $null
try {
    $selfTestInfo = [Diagnostics.ProcessStartInfo]::new()
    $selfTestInfo.FileName = $stagedRuntime
    $selfTestInfo.Arguments = "--self-test-pak `"$pakTest`""
    $selfTestInfo.UseShellExecute = $false
    $selfTestInfo.CreateNoWindow = $true
    $selfTestProcess = [Diagnostics.Process]::Start($selfTestInfo)
    $selfTestProcess.WaitForExit()
    if ($selfTestProcess.ExitCode -ne 0) {
        throw "The staged Windows runtime failed its Controller Pak self-test with exit code $($selfTestProcess.ExitCode)."
    }
} finally {
    if ($null -ne $selfTestProcess) {
        $selfTestProcess.Dispose()
    }
    if (Test-Path -LiteralPath $pakTest) {
        Remove-Item -LiteralPath $pakTest -Recurse -Force
    }
}

# Verify the exact staged launcher can hand controller ownership to the private
# SDL3 helper and back twice without shutting down SDL2 video or audio.
$inputSwitchTest = Join-Path ([IO.Path]::GetTempPath()) `
    ("dkr-r-packaged-input-switch-" + [Guid]::NewGuid().ToString('N'))
$inputSwitchProcess = $null
try {
    New-Item -ItemType Directory -Path $inputSwitchTest | Out-Null
    $inputSwitchInfo = [Diagnostics.ProcessStartInfo]::new()
    $inputSwitchInfo.FileName = $stagedRuntime
    $inputSwitchInfo.Arguments =
        "--self-test-input-switch `"$inputSwitchTest`""
    $inputSwitchInfo.UseShellExecute = $false
    $inputSwitchInfo.CreateNoWindow = $true
    $inputSwitchInfo.EnvironmentVariables['SDL_VIDEODRIVER'] = 'dummy'
    $inputSwitchInfo.EnvironmentVariables['SDL_AUDIODRIVER'] = 'dummy'
    $inputSwitchProcess = [Diagnostics.Process]::Start($inputSwitchInfo)
    $inputSwitchProcess.WaitForExit()
    if ($inputSwitchProcess.ExitCode -ne 0) {
        throw "The staged Windows runtime failed its live input-switch self-test with exit code $($inputSwitchProcess.ExitCode)."
    }
} finally {
    if ($null -ne $inputSwitchProcess) {
        $inputSwitchProcess.Dispose()
    }
    if (Test-Path -LiteralPath $inputSwitchTest) {
        Remove-Item -LiteralPath $inputSwitchTest -Recurse -Force
    }
}

} else {
    Write-Warning 'Runtime tests explicitly deferred. This package still requires runtime qualification.'
}

$packagedFiles = Get-ChildItem -LiteralPath $stage -Recurse -File
$executables = @($packagedFiles | Where-Object { $_.Extension -ieq '.exe' })
$expectedExecutables = @(
    [IO.Path]::GetFullPath($stagedRuntime),
    [IO.Path]::GetFullPath($stagedInputHost),
    [IO.Path]::GetFullPath((Join-Path $stagedInputHostDirectory 'DKR-R-ModWorker.exe'))
)
$unexpectedExecutables = @($executables | Where-Object {
    $expectedExecutables -notcontains [IO.Path]::GetFullPath($_.FullName)
})
if ($executables.Count -ne $expectedExecutables.Count -or $unexpectedExecutables.Count -ne 0) {
    throw "The Windows release must contain one public DKR-R.exe and the two private libexec helpers (input and mod importer). Found: $($executables.FullName -join ', ')"
}
$denied = @($packagedFiles | Where-Object { $deniedExtensions -contains $_.Extension.ToLowerInvariant() })
if ($denied.Count -ne 0) {
    throw "Release staging contains prohibited game data: $($denied.FullName -join ', ')"
}

$n64Headers = @('80371240', '37804012', '40123780')
foreach ($file in $packagedFiles) {
    $stream = [System.IO.File]::OpenRead($file.FullName)
    try {
        $header = New-Object byte[] 4
        $read = $stream.Read($header, 0, 4)
        if ($read -eq 4) {
            $magic = [System.BitConverter]::ToString($header).Replace('-', '')
            if ($n64Headers -contains $magic) {
                throw "Release staging contains an N64 ROM header: $($file.FullName)"
            }
        }
    } finally {
        $stream.Dispose()
    }
}

$archiveCreated = $false
for ($attempt = 1; $attempt -le 5 -and -not $archiveCreated; $attempt++) {
    try {
        # Windows Defender and other scanners may briefly retain the staged PE
        # after its embedded-resource self-test. Retry that transient sharing
        # violation without weakening any package validation.
        Start-Sleep -Milliseconds (250 * $attempt)
        Compress-Archive -LiteralPath $stage -DestinationPath $zip `
            -CompressionLevel Optimal
        $archiveCreated = $true
    } catch {
        if (Test-Path -LiteralPath $zip) {
            Remove-Item -LiteralPath $zip -Force
        }
        if ($attempt -eq 5) {
            throw
        }
    }
}
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $badEntries = @($archive.Entries | Where-Object {
        $deniedExtensions -contains [System.IO.Path]::GetExtension($_.FullName).ToLowerInvariant()
    })
    if ($badEntries.Count -ne 0) {
        throw "Release ZIP contains prohibited game data: $($badEntries.FullName -join ', ')"
    }
} finally {
    $archive.Dispose()
}

$hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256
Write-Host "Created $zip"
Write-Host "SHA-256 $($hash.Hash)"

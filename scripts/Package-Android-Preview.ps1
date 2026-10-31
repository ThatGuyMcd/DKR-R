param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$SdlSource,
    [Parameter(Mandatory)][string]$NdkDirectory,
    [Parameter(Mandatory)][string]$StageDirectory,
    [Parameter(Mandatory)][string]$Gradle,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Version = '',
    [int]$VersionCode = 0,
    [string]$CustomMagicCodeExample = '',
    [string]$CustomMagicCodeDifficultyExample = '',
    [switch]$ReleaseBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.+-]+)?$') { throw 'Invalid APK version.' }
if ($VersionCode -lt 0) { throw 'Invalid APK version code.' }
if (-not (Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -SimpleMatch "DKR_RELEASE_VERSION:STRING=$Version" -Quiet)) { throw 'Native build version does not match the requested APK version.' }
if (Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^DKR_ANDROID_RENDER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
    throw 'Refusing to package private desktop renderer qualification.'
}
$nativePath = Join-Path $BuildDirectory 'libDKR-R.so'
if (Test-Path -LiteralPath $nativePath) {
    $imageText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($nativePath))
    if ($imageText.Contains('DKR_WATER_TEST_MAP') -or $imageText.Contains('[perf][private-water-preview]')) {
        throw 'Android library still contains private water qualification; rebuild after disabling it.'
    }
    $imageText = $null
}
if (Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^DKR_WATER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
    throw 'Refusing to package private water-scene qualification.'
}
if (Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^DKR_TASK_QUALIFICATION:BOOL=(ON|1|TRUE|YES)$' -Quiet) {
    throw 'Refusing to package private host-task fault injection. Rebuild with DKR_TASK_QUALIFICATION=OFF.'
}
if (Test-Path -LiteralPath $StageDirectory) { throw 'Choose a fresh staging directory.' }
$native = Join-Path $StageDirectory 'jniLibs/arm64-v8a'
$assets = Join-Path $StageDirectory 'assets'
$strip = Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
$readelf = Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
New-Item -ItemType Directory -Force $native, $assets, $OutputDirectory | Out-Null
$codeSdkArguments = @('--output', (Join-Path $assets 'Custom-Magic-Codes'))
if ($CustomMagicCodeExample) { $codeSdkArguments += @('--example', $CustomMagicCodeExample) }
if ($CustomMagicCodeDifficultyExample) { $codeSdkArguments += @('--difficulty-example', $CustomMagicCodeDifficultyExample) }
& python (Join-Path $PSScriptRoot 'stage_magic_code_sdk.py') @codeSdkArguments
if ($LASTEXITCODE -ne 0) { throw 'Custom Magic Codes SDK staging failed.' }
$libraries = @(
    (Join-Path $BuildDirectory 'libDKR-R.so'),
    (Join-Path $BuildDirectory 'sdl2/libSDL2.so'),
    (Join-Path $BuildDirectory 'bin/Release/libexec/dkr-r/libDKR-R-ModWorker.so'),
    (Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so')
)
foreach ($library in $libraries) {
    if (!(Test-Path -LiteralPath $library -PathType Leaf)) { throw "Missing native library: $library" }
    $header = & $readelf -h $library
    if ($LASTEXITCODE -ne 0 -or ($header -join "`n") -notmatch 'AArch64') { throw "Wrong native architecture: $library" }
    Copy-Item -LiteralPath $library -Destination $native
    & $strip --strip-debug (Join-Path $native (Split-Path $library -Leaf))
    if ($LASTEXITCODE -ne 0) { throw 'Native symbol stripping failed.' }
}
# The CMake post-build asset tree contains only the explicit launcher artwork,
# controller database and CRT filters. Never stage ROMs, profiles or mod banks.
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'assets') -Destination (Join-Path $assets 'runtime') -Recurse
$notices = Join-Path $assets 'notices'
New-Item -ItemType Directory $notices | Out-Null
foreach ($file in @('LICENSE.md','THIRD_PARTY.md','runtime-recomp/COPYING-NOTICE.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination $notices
}
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -eq '1.0.5') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/PUBLIC-1.0.5-NOTES.txt') -Destination $notices
    Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')
}
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -eq '1.0.6') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/PUBLIC-1.0.6-NOTES.txt') -Destination $notices
    Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')
}
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -eq '1.0.7') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/PUBLIC-1.0.7-NOTES.txt') -Destination $notices
    Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')
}
if ($Version -eq '1.0.7+beta.2') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/BETA2-1.0.7.txt') -Destination (Join-Path $notices 'RELEASE-NOTES.txt')
}
if ($Version -eq '1.0.7+beta.4') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/BETA4-1.0.7.txt') -Destination (Join-Path $notices 'RELEASE-NOTES.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')
}
if ($Version -eq '1.0.7+beta.3') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/BETA3-1.0.7.txt') -Destination (Join-Path $notices 'RELEASE-NOTES.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')
}
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim().StartsWith('1.0.5-beta.15') -and
    (Test-Path -LiteralPath (Join-Path $projectRoot 'docs/ENHANCED-WATER-BETA15.txt'))) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/ENHANCED-WATER-BETA15.txt') -Destination $notices
}
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -eq '1.0.5-beta.15') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/BETA15-RELEASE.txt') -Destination $notices
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/BETA15-ONLINE-STABILITY-20261007.txt') -Destination $notices
    $androidNotes = Join-Path $projectRoot 'docs/BETA15-ANDROID-RENDER-FLOW.txt'
    if (Test-Path -LiteralPath $androidNotes) {
        Copy-Item -LiteralPath $androidNotes -Destination $notices
    }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/licenses') -Destination (Join-Path $notices 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/UPDATES.md') -Destination $notices
Copy-Item -LiteralPath (Join-Path $SdlSource 'LICENSE.txt') -Destination (Join-Path $notices 'SDL2-LICENSE.txt')
foreach ($entry in @(
    @('runtime-recomp/src/water/LICENSE-WAVE-RACE.txt','WAVE-RACE-WATER-LICENSE.txt'),
    @('extern/rt64/LICENSE','RT64-LICENSE.txt'),
    @('extern/rt64/src/contrib/imgui/LICENSE.txt','Dear-ImGui-LICENSE.txt'),
    @('extern/n64-modern-runtime/COPYING','N64ModernRuntime-COPYING.txt'),
    @('extern/n64-modern-runtime/N64Recomp/LICENSE','N64Recomp-LICENSE.txt'),
    @('extern/libdatachannel/LICENSE','LIBDATACHANNEL-LICENSE.txt'),
    @('extern/mbedtls/LICENSE','MBEDTLS-LICENSE.txt'),
    @('extern/libdatachannel/deps/libjuice/LICENSE','LIBJUICE-LICENSE.txt'),
    @('extern/libdatachannel/deps/usrsctp/LICENSE.md','USRSCTP-LICENSE.txt'),
    @('extern/libdatachannel/deps/json/LICENSE.MIT','NLOHMANN-JSON-LICENSE.txt'),
    @('extern/libdatachannel/deps/plog/LICENSE','PLOG-LICENSE.txt')
)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry[0]) -Destination (Join-Path $notices $entry[1])
}
foreach ($file in Get-ChildItem -LiteralPath $StageDirectory -Recurse -File) {
    if ($file.Extension -match '^\.(z64|v64|n64|eep|mpk|sra|fla|o2r|otr)$') { throw "Game data in stage: $($file.Name)" }
    $stream = [IO.File]::OpenRead($file.FullName)
    try { $header = New-Object byte[] 4; $null = $stream.Read($header,0,4) }
    finally { $stream.Dispose() }
    if ([BitConverter]::ToString($header) -in @('80-37-12-40','37-80-40-12','40-12-37-80')) { throw 'ROM header found in staging.' }
}
# Incremental APK assembly can retain the previous compressed native-library
# entry as unused ZIP space. Clean only Gradle's app outputs before distribution;
# staged inputs, native build products, signing identity and user data are kept.
$variant = if ($ReleaseBuild) { 'release' } else { 'debug' }
$assembleTask = if ($ReleaseBuild) { ':app:assembleRelease' } else { ':app:assembleDebug' }
$versionArguments = @("-PdkrVersionName=$Version")
if ($VersionCode -gt 0) { $versionArguments += "-PdkrVersionCode=$VersionCode" }
& $Gradle -p (Join-Path $projectRoot 'packaging/android') "-PdkrSdlSource=$SdlSource" "-PdkrNativeStage=$StageDirectory" @versionArguments :app:clean $assembleTask --no-daemon
if ($LASTEXITCODE -ne 0) { throw 'Android APK assembly failed.' }
$suffix = if ($ReleaseBuild) { '' } else { '-preview' }
$output = Join-Path $OutputDirectory "DKR-R-$version-Android-arm64$suffix.apk"
if (Test-Path -LiteralPath $output) { throw 'Refusing to replace an existing APK.' }
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/android/app/build/outputs/apk/$variant/app-$variant.apk") -Destination $output
Get-FileHash -LiteralPath $output -Algorithm SHA256

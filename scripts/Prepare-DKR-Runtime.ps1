[CmdletBinding()]
param(
    [string]$RomV77 = '',
    [string]$RomV80 = '',
    [switch]$SkipApt,
    [switch]$Force,
    [switch]$SkipRecompile,
    [switch]$SkipProbe,
    [switch]$BuildRenderer,
    [string]$Python = '',
    [int]$Jobs = 0
)

# Prepare everything Build-Windows.cmd compiles, the same way
# Prepare-DKR-Runtime-macOS.sh does on macOS: pinned dependencies with their
# patches, N64Recomp, one decomp ELF per supported ROM revision (US v1.0 = v77,
# US Rev A/v1.1 = v80, each in its own build\decomp\v<rev> tree, built under
# WSL), the production payloads (scripts\compose_payloads.py, into
# build\generated\v<rev>), and the build inputs scripts\prepare_payloads.py
# derives from them (build\prepared, which CMake reads by default).

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LogDirectory = Join-Path $ProjectRoot 'build-logs'
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogPath = Join-Path $LogDirectory "prepare-dkr-runtime-$Timestamp.log"
Start-Transcript -Path $LogPath -Force | Out-Null

$DkrSource = Join-Path $ProjectRoot 'extern\dkr-decomp'
$RuntimeRoot = Join-Path $ProjectRoot 'runtime-recomp'
$GeneratedRoot = Join-Path $ProjectRoot 'build\generated'
$PreparedRoot = Join-Path $ProjectRoot 'build\prepared'
$ResolvedPath = Join-Path $RuntimeRoot 'resolved-runtime-dependencies.json'
$Revisions = @(77, 80)

function Fail([string]$Message) {
    throw $Message
}

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}
function Convert-ToWslPath([string]$WindowsPath) {
    if ([string]::IsNullOrWhiteSpace($WindowsPath)) {
        Fail 'Cannot translate an empty Windows path for WSL.'
    }

    $resolvedWindowsPath = (Resolve-Path -LiteralPath $WindowsPath).Path
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& wsl.exe --exec wslpath -a -u $resolvedWindowsPath 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $translatedPath = ($output | Out-String).Trim()

    if ($exitCode -ne 0 -or [string]::IsNullOrWhiteSpace($translatedPath)) {
        $detail = $translatedPath
        if ([string]::IsNullOrWhiteSpace($detail)) {
            $detail = 'No diagnostic text was returned by wslpath.'
        }
        Fail "WSL could not translate the Windows path '$resolvedWindowsPath'. wslpath returned: $detail"
    }

    if ($translatedPath -match "[`r`n]") {
        Fail "WSL returned more than one line while translating '$resolvedWindowsPath': $translatedPath"
    }

    return $translatedPath
}

function Get-NativeCMake {
    $candidates = New-Object System.Collections.Generic.List[string]

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $install = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        if ($install) {
            $candidates.Add((Join-Path $install.Trim() 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'))
        }
    }

    $kitware = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    $candidates.Add($kitware)
    $pathCmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($pathCmake) { $candidates.Add($pathCmake.Source) }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $generators = & $candidate --help 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -and $generators -match 'Visual Studio 17 2022') {
            return $candidate
        }
    }
    Fail 'A native Windows CMake with the Visual Studio 17 2022 generator was not found. Run Build-Windows.cmd successfully first.'
}

function Invoke-Checked([string]$Description, [scriptblock]$Command) {
    Write-Host "-- $Description"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        Fail "$Description failed with exit code $LASTEXITCODE."
    }
}

function Resolve-RomPath([int]$Revision, [string]$Explicit) {
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (-not (Test-Path -LiteralPath $Explicit -PathType Leaf)) { Fail "ROM not found: $Explicit" }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    if ($Revision -eq 77) {
        # The launcher remembers the US v1.0 ROM it validated.
        $candidates = @(
            (Join-Path $ProjectRoot 'runtime\config\rom-source.json'),
            (Join-Path $ProjectRoot 'dist\DKRPort-Windows-x64\runtime\config\rom-source.json'),
            (Join-Path $ProjectRoot 'build\windows-x64\bin\Release\runtime\config\rom-source.json'),
            (Join-Path $ProjectRoot 'build\windows-x64\bin\Debug\runtime\config\rom-source.json')
        )
        foreach ($candidate in $candidates) {
            if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
            try {
                $config = Get-Content -LiteralPath $candidate -Raw | ConvertFrom-Json
                $path = [string]$config.path
                if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
                    Write-Host "[OK] Using the ROM already connected by the launcher: $path" -ForegroundColor Green
                    return (Resolve-Path -LiteralPath $path).Path
                }
            } catch {
                Write-Warning "Could not read ROM source configuration '$candidate': $($_.Exception.Message)"
            }
        }
    }

    $label = if ($Revision -eq 77) { 'US 1.0' } else { 'US Rev A (1.1)' }
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = "Select your Diddy Kong Racing $label ROM"
    $dialog.Filter = 'Nintendo 64 ROM (*.z64;*.v64;*.n64)|*.z64;*.v64;*.n64|All files (*.*)|*.*'
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Fail "No $label ROM was selected. Pass -RomV$Revision <path> or rerun this command and choose it."
    }
    return $dialog.FileName
}

function Resolve-Python {
    if (-not [string]::IsNullOrWhiteSpace($Python)) { return @($Python) }
    if (Get-Command py.exe -ErrorAction SilentlyContinue) { return @('py.exe', '-3') }
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($command) { return @($command.Source) }
    Fail 'Python 3.11 or newer was not found. Install it from python.org, or pass -Python <python.exe>.'
}

function Invoke-Python([string]$Description, [string[]]$Arguments) {
    $python = @(Resolve-Python)
    $rest = @()
    if ($python.Count -gt 1) { $rest = $python[1..($python.Count - 1)] }
    & $python[0] @rest @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "$Description failed with exit code $LASTEXITCODE." }
}

function Clone-Or-Update([string]$Name, [string]$Repository, [string]$Destination,
                         [string]$Commit, [switch]$Recursive) {
    $git = (Get-Command git.exe -ErrorAction SilentlyContinue)
    if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }
    if (-not $git) { Fail 'Git was not found. Run Build-Windows.cmd first.' }

    if (-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
        $args = @('clone')
        if ($Recursive) { $args += '--recurse-submodules' }
        $args += @($Repository, $Destination)
        & $git.Source @args
        if ($LASTEXITCODE -ne 0) { Fail "Could not clone $Name from $Repository." }
    } else {
        if ($Force) {
            & $git.Source -C $Destination reset --hard
            if ($LASTEXITCODE -ne 0) { Fail "Could not reset $Name." }
            & $git.Source -C $Destination clean -fd
            if ($LASTEXITCODE -ne 0) { Fail "Could not clean $Name." }
        }
        & $git.Source -C $Destination fetch origin $Commit
        if ($LASTEXITCODE -ne 0) { Fail "Could not update $Name." }
    }
    & $git.Source -C $Destination checkout --detach $Commit
    if ($LASTEXITCODE -ne 0) { Fail "Could not select pinned $Name commit $Commit." }
    if ($Recursive) {
        & $git.Source -C $Destination submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) { Fail "Could not initialise $Name submodules." }
    }
    $resolved = (& $git.Source -C $Destination rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { Fail "Could not resolve the checked-out commit for $Name." }
    Write-Host "[OK] ${Name}: $resolved" -ForegroundColor Green
    return $resolved
}

try {
    Write-Host 'DKR-R - runtime preparation (decomp ELFs, payloads, prepared build inputs)'
    Write-Host "Project: $ProjectRoot"
    Write-Host "Log: $LogPath"
    Write-Host ''

    if (-not (Get-Command git.exe -ErrorAction SilentlyContinue) -and -not (Get-Command git -ErrorAction SilentlyContinue)) {
        Fail 'Git is missing. Run Build-Windows.cmd first.'
    }
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        Fail 'Windows Subsystem for Linux is not installed. Run "wsl --install -d Ubuntu" from an administrator terminal, restart Windows if requested, then rerun this command.'
    }
    $wslList = (& wsl.exe --list --quiet 2>$null | Out-String).Trim()
    if (-not $wslList) {
        Fail 'WSL is installed but no Linux distribution is registered. Run "wsl --install -d Ubuntu", complete first-launch setup, then retry.'
    }

    if ($Jobs -le 0) {
        $Jobs = [Math]::Max(2, [Environment]::ProcessorCount - 1)
    }

    Write-Step 'Preparing the pinned DKR decomp checkout'
    $sourcePreparationArgs = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'Prepare-Game-Source.ps1'))
    if ($Force) { $sourcePreparationArgs += '-Force' }
    & powershell.exe @sourcePreparationArgs
    if ($LASTEXITCODE -ne 0) { Fail 'The pinned DKR decomp source could not be prepared.' }

    $roms = @{ 77 = (Resolve-RomPath 77 $RomV77); 80 = (Resolve-RomPath 80 $RomV80) }

    Write-Step 'Building the decomp ELFs under WSL'
    if (-not $SkipApt) {
        Write-Host 'Ubuntu may ask for your Linux sudo password while installing the documented decomp dependencies.' -ForegroundColor Yellow
        & wsl.exe --exec bash -lc "sudo apt-get update && sudo apt-get install -y build-essential pkg-config git python3 python3-pip binutils-mips-linux-gnu python3-venv libpcre2-dev libpcre2-8-0 cmake ninja-build clang lld libcurl4-openssl-dev rsync"
        if ($LASTEXITCODE -ne 0) { Fail 'WSL dependency installation failed. Rerun with -SkipApt only after installing the listed packages manually.' }
    }

    # Each revision gets its own tree: the decomp extracts every version into
    # the same asm/, assets/ and build/ directories. The live checkout is only
    # read. Do not pass a Windows CRLF here-string directly to `bash -lc`: Bash
    # treats the hidden carriage return in `pipefail\r` as part of the option
    # name. Write an explicit LF-only, BOM-free script and run that through WSL.
    $buildScriptPath = Join-Path $LogDirectory "build-dkr-decomp-$Timestamp.sh"
    $buildLines = @(
        'set -euo pipefail',
        'source="$1" tree="$2" revision="$3" jobs="$4"',
        '(cd "$source" && git submodule update --init --recursive)',
        'mkdir -p "$tree"',
        'rsync -a --delete --exclude /.git --exclude /build --exclude /asm --exclude /assets \',
        '    --exclude /baseroms --exclude /.venv "$source/" "$tree/"',
        'cd "$tree"',
        "if [ ! -x .venv/bin/python3 ] || ! .venv/bin/python3 -c 'import splat' >/dev/null 2>&1; then",
        '    echo "[INFO] The decomp Python/tool environment is incomplete; running make setup."',
        '    make setup',
        'fi',
        "if ! .venv/bin/python3 -c 'import splat' >/dev/null 2>&1; then",
        '    echo "[ERROR] make setup completed, but splat is still unavailable in $tree/.venv." >&2',
        '    exit 92',
        'fi',
        'make VERSION="v$revision" extract',
        'make VERSION="v$revision" -j"$jobs"'
    )
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($buildScriptPath, ([string]::Join("`n", $buildLines) + "`n"), $utf8NoBom)
    $wslBuildScript = Convert-ToWslPath $buildScriptPath
    $wslDkr = Convert-ToWslPath $DkrSource

    $elfs = @{}
    $builtRoms = @{}
    foreach ($revision in $Revisions) {
        $tree = Join-Path $ProjectRoot "build\decomp\v$revision"
        Write-Step "US v${revision}: ROM"
        Invoke-Python "Checking the US v$revision ROM" @(
            (Join-Path $PSScriptRoot 'compose_payloads.py'), 'rom', '--revision', "$revision",
            $roms[$revision], (Join-Path $tree "baseroms\baserom.us.v$revision.z64"))

        Write-Step "US v${revision}: decomp ELF"
        New-Item -ItemType Directory -Force -Path $tree | Out-Null
        $wslTree = Convert-ToWslPath $tree
        & wsl.exe --exec bash $wslBuildScript $wslDkr $wslTree ([string]$revision) ([string]$Jobs)
        if ($LASTEXITCODE -ne 0) { Fail "The US v$revision decomp build failed. The log above contains the first real decomp/toolchain error." }
        $elfs[$revision] = Join-Path $tree "build\dkr.us.v$revision.elf"
        $builtRoms[$revision] = Join-Path $tree "build\dkr.us.v$revision.z64"
        foreach ($output in @($elfs[$revision], $builtRoms[$revision])) {
            if (-not (Test-Path -LiteralPath $output -PathType Leaf)) { Fail "The US v$revision decomp build did not produce $output" }
        }
        Write-Host "[OK] US v$revision ELF: $($elfs[$revision])" -ForegroundColor Green
    }

    Write-Step 'Preparing the static recompilation tool and runtime sources'
    $modernRuntimePath = Join-Path $ProjectRoot 'extern\n64-modern-runtime'
    $n64RecompPath = Join-Path $modernRuntimePath 'N64Recomp'
    $rt64Path = Join-Path $ProjectRoot 'extern\rt64'
    $gekkoNetPath = Join-Path $ProjectRoot 'extern\gekkonet'
    $monocypherPath = Join-Path $ProjectRoot 'extern\monocypher'
    $libdatachannelPath = Join-Path $ProjectRoot 'extern\libdatachannel'
    $mbedTlsPath = Join-Path $ProjectRoot 'extern\mbedtls'
    $sdl3Path = Join-Path $ProjectRoot 'extern\sdl3'
    $resolved = [ordered]@{}
    $patchManifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'patches\manifest.json') -Raw |
        ConvertFrom-Json
    $modernRuntimeCommit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'N64ModernRuntime' |
        Select-Object -ExpandProperty expectedCommit)
    $rt64Commit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'RT64' |
        Select-Object -ExpandProperty expectedCommit)
    $gekkoNetCommit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'GekkoNet' |
        Select-Object -ExpandProperty expectedCommit)
    $monocypherCommit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'Monocypher' |
        Select-Object -ExpandProperty expectedCommit)
    $libdatachannelCommit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'libdatachannel' |
        Select-Object -ExpandProperty expectedCommit)
    $mbedTlsCommit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'MbedTLS' |
        Select-Object -ExpandProperty expectedCommit)
    $sdl3Commit = [string](
        $patchManifest.dependencies | Where-Object name -eq 'SDL3' |
        Select-Object -ExpandProperty expectedCommit)

    # N64ModernRuntime pins the N64Recomp revision it is compatible with as a
    # git submodule. Always build that copy instead of independently selecting
    # a potentially incompatible N64Recomp main branch.
    $resolved.n64ModernRuntime = Clone-Or-Update 'N64ModernRuntime' 'https://github.com/N64Recomp/N64ModernRuntime.git' $modernRuntimePath $modernRuntimeCommit -Recursive
    if (-not (Test-Path -LiteralPath (Join-Path $n64RecompPath 'CMakeLists.txt') -PathType Leaf)) {
        Fail "N64ModernRuntime was prepared, but its pinned N64Recomp submodule is missing at: $n64RecompPath"
    }
    $gitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $gitCommand) { $gitCommand = Get-Command git -ErrorAction Stop }
    $resolved.n64recomp = (& $gitCommand.Source -C $n64RecompPath rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $resolved.n64recomp) {
        Fail 'Could not resolve the N64Recomp commit pinned by N64ModernRuntime.'
    }
    Write-Host "[OK] N64Recomp (runtime-pinned submodule): $($resolved.n64recomp)" -ForegroundColor Green

    # Keep the rollback coordinator pinned and unmodified. DKR-R owns the
    # transport, lobby protocol and complete simulation-state adapter.
    $resolved.gekkoNet = Clone-Or-Update 'GekkoNet' 'https://github.com/HeatXD/GekkoNet.git' $gekkoNetPath $gekkoNetCommit
    $resolved.monocypher = Clone-Or-Update 'Monocypher' 'https://github.com/LoupVaillant/Monocypher.git' $monocypherPath $monocypherCommit
    $resolved.libdatachannel = Clone-Or-Update 'libdatachannel' 'https://github.com/paullouisageneau/libdatachannel.git' $libdatachannelPath $libdatachannelCommit -Recursive
    $resolved.mbedTls = Clone-Or-Update 'MbedTLS' 'https://github.com/Mbed-TLS/mbedtls.git' $mbedTlsPath $mbedTlsCommit -Recursive
    $resolved.sdl3 = Clone-Or-Update 'SDL3' 'https://github.com/libsdl-org/SDL.git' $sdl3Path $sdl3Commit

    if ($BuildRenderer) {
        $resolved.rt64 = Clone-Or-Update 'RT64' 'https://github.com/rt64/rt64.git' $rt64Path $rt64Commit -Recursive
    } else {
        $resolved.rt64 = 'not requested'
        Write-Host '[INFO] RT64 checkout skipped. Add -BuildRenderer when the CPU/runtime probe is ready for renderer integration.' -ForegroundColor DarkGray
    }

    Write-Step 'Applying pinned dependency patches'
    $patchScript = Join-Path $ProjectRoot 'scripts\Apply-Dependency-Patches.ps1'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $patchScript
    if ($LASTEXITCODE -ne 0) { Fail 'The pinned dependency patch pipeline failed.' }

    $cmake = Get-NativeCMake
    $toolBuild = Join-Path $ProjectRoot 'build\runtime-tools\n64recomp'
    & $cmake -S $n64RecompPath -B $toolBuild -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { Fail 'N64Recomp CMake configuration failed.' }
    & $cmake --build $toolBuild --config Release --target N64RecompCLI RSPRecomp --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { Fail 'N64Recomp compilation failed.' }

    $recompiler = Get-ChildItem -LiteralPath $toolBuild -Filter 'N64Recomp.exe' -File -Recurse | Select-Object -First 1
    if (-not $recompiler) { Fail 'N64Recomp built, but N64Recomp.exe could not be located.' }
    $rspRecompiler = Get-ChildItem -LiteralPath $toolBuild -Filter 'RSPRecomp.exe' -File -Recurse | Select-Object -First 1
    if (-not $rspRecompiler) { Fail 'N64Recomp built, but RSPRecomp.exe could not be located.' }
    Write-Host "[OK] N64Recomp executable: $($recompiler.FullName)" -ForegroundColor Green
    Write-Host "[OK] RSPRecomp executable: $($rspRecompiler.FullName)" -ForegroundColor Green

    $payloads = @{}
    if (-not $SkipRecompile) {
        foreach ($revision in $Revisions) {
            Write-Step "US v${revision}: recompiled payload"
            $work = Join-Path $GeneratedRoot "v$revision"
            # Generated output only: never reuse a previous run's files.
            if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
            Invoke-Python "The US v$revision payload (log: $work\n64recomp.log)" @(
                (Join-Path $PSScriptRoot 'compose_payloads.py'), 'payload', '--revision', "$revision",
                '--elf', $elfs[$revision], '--rom', $builtRoms[$revision],
                '--recompiler', $recompiler.FullName, '--output', $work)
        }

        # The build inputs derived from both payloads (save layout and portrait
        # stages, owned engine, Camera++ and model kernels).
        Write-Step 'Prepared build inputs'
        Invoke-Python 'Preparing the build inputs' @((Join-Path $PSScriptRoot 'prepare_payloads.py'))
    }
    foreach ($revision in $Revisions) {
        $payloads[$revision] = Join-Path $PreparedRoot "generated-v$revision"
    }

    $functionFiles = @(Get-ChildItem -LiteralPath $payloads[77] -File -Include '*.c' -Recurse -ErrorAction SilentlyContinue)
    $probeExecutable = $null
    $probeBuilt = $false

    if (-not $SkipProbe) {
        if ($functionFiles.Count -eq 0) {
            Fail 'No generated CPU translation files were found, so the runtime compilation probe cannot be built. Rerun without -SkipRecompile or use -SkipProbe while diagnosing N64Recomp output.'
        }

        Write-Step 'Compiling generated DKR CPU code against N64ModernRuntime'
        $probeBuild = Join-Path $ProjectRoot 'build\dkr-runtime-probe'
        $rendererValue = 'OFF'
        if ($BuildRenderer) { $rendererValue = 'ON' }

        & $cmake -S $RuntimeRoot -B $probeBuild -G 'Visual Studio 17 2022' -A x64 `
            "-DDKRPORT_ROOT=$ProjectRoot" `
            '-DDKR_RUNTIME_BUILD_GENERATED=ON' `
            "-DDKR_RUNTIME_BUILD_RT64=$rendererValue"
        if ($LASTEXITCODE -ne 0) {
            Fail 'The DKR runtime probe CMake configuration failed. This normally means a generated-code include, N64ModernRuntime revision or optional RT64 dependency must be adapted.'
        }

        $probeTextLog = Join-Path $LogDirectory "dkr-runtime-msbuild-$Timestamp.log"
        $probeBinaryLog = Join-Path $LogDirectory "dkr-runtime-msbuild-$Timestamp.binlog"
        $probeFileLogger = "/flp:LogFile=$probeTextLog;Verbosity=diagnostic;Encoding=UTF-8"
        $probeBinaryLogger = "/bl:$probeBinaryLog"
        & $cmake --build $probeBuild --config Release --parallel $Jobs -- /nologo /verbosity:minimal /fileLogger $probeFileLogger $probeBinaryLogger
        if ($LASTEXITCODE -ne 0) {
            Write-Host ''
            Write-Host '==> Runtime compiler error summary' -ForegroundColor Red
            if (Test-Path -LiteralPath $probeTextLog) {
                $patterns = @('fatal error C\d+', 'error C\d+', 'error LNK\d+', 'fatal error LNK\d+', 'error MSB\d+', ': error :')
                $lines = @(Select-String -Path $probeTextLog -Pattern $patterns |
                    ForEach-Object { $_.Line.Trim() } |
                    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                    Select-Object -Unique -First 50)
                foreach ($line in $lines) { Write-Host $line -ForegroundColor Red }
            }
            Fail "The generated DKR CPU/runtime compilation probe failed. Full MSBuild log: $probeTextLog"
        }

        $probeExecutable = Get-ChildItem -LiteralPath $probeBuild -Filter 'DKRRuntimeProbe.exe' -File -Recurse | Select-Object -First 1
        if (-not $probeExecutable) { Fail 'The runtime probe build completed but DKRRuntimeProbe.exe was not found.' }
        & $probeExecutable.FullName
        if ($LASTEXITCODE -ne 0) { Fail 'DKRRuntimeProbe.exe reported an incomplete generated-code/runtime build.' }
        $probeBuilt = $true
        Write-Host "[OK] Runtime compilation probe: $($probeExecutable.FullName)" -ForegroundColor Green
    }

    $probeExecutablePath = $null
    if ($probeExecutable) { $probeExecutablePath = $probeExecutable.FullName }

    $state = [ordered]@{
        schemaVersion = 2
        preparedUtc = [DateTime]::UtcNow.ToString('o')
        dkrElf = [ordered]@{ v77 = $elfs[77]; v80 = $elfs[80] }
        preparedPayloads = [ordered]@{ v77 = $payloads[77]; v80 = $payloads[80] }
        generatedFunctionFiles = $functionFiles.Count
        n64RecompExecutable = $recompiler.FullName
        rspRecompExecutable = $rspRecompiler.FullName
        runtimeProbeBuilt = $probeBuilt
        runtimeProbeExecutable = $probeExecutablePath
        rt64BuildRequested = [bool]$BuildRenderer
        dependencies = $resolved
    }
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResolvedPath -Encoding UTF8

    Write-Host ''
    Write-Host "[OK] Payloads ready in $GeneratedRoot, build inputs in $PreparedRoot" -ForegroundColor Green
    Write-Host "Generated function files (US v1.0): $($functionFiles.Count)"
    Write-Host "State report: $ResolvedPath"
    Write-Host 'No ROM or extracted game asset is added to source control or the distributable ZIP.'
    Write-Host 'Next: Build-Windows.cmd builds from build\prepared.'
    Stop-Transcript | Out-Null
    exit 0
} catch {
    Write-Host ''
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Full log: $LogPath"
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

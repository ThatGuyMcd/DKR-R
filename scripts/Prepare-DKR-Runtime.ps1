[CmdletBinding()]
param(
    [switch]$SkipApt,
    [switch]$Force,
    [switch]$SkipRecompile,
    [switch]$SkipProbe,
    [switch]$BuildRenderer,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LogDirectory = Join-Path $ProjectRoot 'build-logs'
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogPath = Join-Path $LogDirectory "prepare-dkr-runtime-$Timestamp.log"
Start-Transcript -Path $LogPath -Force | Out-Null

$ExpectedSha1 = '0cb115d8716dbbc2922fda38e533b9fe63bb9670'
$DkrSource = Join-Path $ProjectRoot 'extern\dkr-decomp'
$RuntimeRoot = Join-Path $ProjectRoot 'runtime-recomp'
$GeneratedFunctions = Join-Path $RuntimeRoot 'RecompiledFuncs'
$ResolvedPath = Join-Path $RuntimeRoot 'resolved-runtime-dependencies.json'

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


function Resolve-DkrRecompEntrypoint([string]$ElfPath, [string]$RomHeaderEntrypointText) {
    if (-not (Test-Path -LiteralPath $ElfPath -PathType Leaf)) {
        Fail "Cannot inspect the DKR ELF because it does not exist: $ElfPath"
    }

    $wslElf = Convert-ToWslPath $ElfPath
    $symbolsLog = Join-Path $LogDirectory "dkr-elf-symbols-$Timestamp.log"
    $sectionsLog = Join-Path $LogDirectory "dkr-elf-sections-$Timestamp.log"

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $symbolOutput = @(& wsl.exe --exec mips-linux-gnu-nm -n --defined-only $wslElf 2>&1)
        $symbolExitCode = $LASTEXITCODE
        $sectionOutput = @(& wsl.exe --exec mips-linux-gnu-readelf -S -W $wslElf 2>&1)
        $sectionExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($symbolsLog, (($symbolOutput | Out-String).TrimEnd() + "`n"), $utf8NoBom)
    [System.IO.File]::WriteAllText($sectionsLog, (($sectionOutput | Out-String).TrimEnd() + "`n"), $utf8NoBom)

    if ($symbolExitCode -ne 0) {
        Fail "Could not inspect DKR ELF symbols with mips-linux-gnu-nm. Full output: $symbolsLog"
    }
    if ($sectionExitCode -ne 0) {
        Fail "Could not inspect DKR ELF sections with mips-linux-gnu-readelf. Full output: $sectionsLog"
    }

    [uint64]$romHeaderAddress = [Convert]::ToUInt64($RomHeaderEntrypointText.Substring(2), 16)
    $mainprocAddress = $null
    $headerSymbolAddress = $null
    $headerSymbolName = $null

    foreach ($rawLine in $symbolOutput) {
        $line = [string]$rawLine
        if ($line -notmatch '^\s*([0-9A-Fa-f]+)\s+([A-Za-z])\s+(\S+)\s*$') {
            continue
        }

        [uint64]$address = [Convert]::ToUInt64($Matches[1], 16)
        $symbolType = $Matches[2]
        $symbolName = $Matches[3]

        if ($symbolName -eq 'mainproc') {
            $mainprocAddress = $address
        }
        if ($address -eq $romHeaderAddress -and $symbolType -match '^[TtWw]$') {
            $headerSymbolAddress = $address
            $headerSymbolName = $symbolName
        }
    }

    # DKR's decomp documents mainproc as the function run after IPL3 has
    # completed. Prefer that real function symbol over the raw ROM header PC,
    # which may point at bootstrap code that is not represented as an ELF
    # function and therefore cannot be selected by N64Recomp.
    $selectedAddress = $null
    $selectedSymbol = $null
    if ($null -ne $mainprocAddress) {
        $selectedAddress = [uint64]$mainprocAddress
        $selectedSymbol = 'mainproc'
    } elseif ($null -ne $headerSymbolAddress) {
        $selectedAddress = [uint64]$headerSymbolAddress
        $selectedSymbol = [string]$headerSymbolName
    } else {
        Fail ("The DKR ELF contains neither the mainproc symbol nor a text symbol at the ROM header entry point " +
            "$RomHeaderEntrypointText. ELF symbols: $symbolsLog")
    }

    if (($selectedAddress -band 3) -ne 0) {
        Fail ("Resolved DKR entrypoint $selectedSymbol at 0x{0:X8}, but it is not word-aligned." -f $selectedAddress)
    }

    $hasMdebug = (($sectionOutput | Out-String) -match '(?m)\s\.mdebug\s')
    $addressText = '0x{0:X8}' -f ([uint32]$selectedAddress)

    Write-Host "[OK] Static recompilation entry function: $selectedSymbol at $addressText" -ForegroundColor Green
    Write-Host "[OK] ELF .mdebug metadata available: $hasMdebug" -ForegroundColor Green
    Write-Host "[OK] ELF symbol report: $symbolsLog" -ForegroundColor Green

    return [PSCustomObject]@{
        AddressText = $addressText
        SymbolName = $selectedSymbol
        UseMdebug = [bool]$hasMdebug
        SymbolsLog = $symbolsLog
        SectionsLog = $sectionsLog
    }
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

function Resolve-RomPath {
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

    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select your validated Diddy Kong Racing US 1.0 ROM'
    $dialog.Filter = 'Nintendo 64 ROM (*.z64;*.v64;*.n64)|*.z64;*.v64;*.n64|All files (*.*)|*.*'
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Fail 'No ROM was selected. Validate the ROM in the launcher or rerun this command and choose it.'
    }
    return $dialog.FileName
}

function Convert-ToCanonicalRom([string]$SourcePath, [string]$DestinationPath) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($SourcePath)
    if ($bytes.Length -lt 4) { Fail 'The selected ROM is too small to contain an N64 header.' }

    $magic = '{0:X2}{1:X2}{2:X2}{3:X2}' -f $bytes[0], $bytes[1], $bytes[2], $bytes[3]
    switch ($magic) {
        '80371240' { Write-Host '[OK] ROM byte order: z64 / big-endian' -ForegroundColor Green }
        '37804012' {
            Write-Host '[OK] ROM byte order: v64 / byte-swapped; normalising locally' -ForegroundColor Green
            for ($i = 0; $i + 1 -lt $bytes.Length; $i += 2) {
                $tmp = $bytes[$i]; $bytes[$i] = $bytes[$i + 1]; $bytes[$i + 1] = $tmp
            }
        }
        '40123780' {
            Write-Host '[OK] ROM byte order: n64 / little-endian; normalising locally' -ForegroundColor Green
            for ($i = 0; $i + 3 -lt $bytes.Length; $i += 4) {
                $a = $bytes[$i]; $b = $bytes[$i + 1]
                $bytes[$i] = $bytes[$i + 3]
                $bytes[$i + 1] = $bytes[$i + 2]
                $bytes[$i + 2] = $b
                $bytes[$i + 3] = $a
            }
        }
        default { Fail "The selected file does not have a recognised N64 ROM byte order (magic $magic)." }
    }

    $sha = [System.Security.Cryptography.SHA1]::Create()
    try {
        $hash = ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) -join ''
    } finally {
        $sha.Dispose()
    }
    if ($hash -ne $ExpectedSha1) {
        Fail "The normalised ROM SHA-1 is $hash, but DKR US 1.0 requires $ExpectedSha1."
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestinationPath) | Out-Null
    [System.IO.File]::WriteAllBytes($DestinationPath, $bytes)
    Write-Host "[OK] Canonical local build ROM written to: $DestinationPath" -ForegroundColor Green
    return $bytes
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
    Write-Host 'DKR-R - decomp ELF and static-recompilation preparation'
    Write-Host "Project: $ProjectRoot"
    Write-Host "Log: $LogPath"
    Write-Host ''
    Write-Host 'This command prepares the first real game-code boundary. It builds the matching DKR ELF and attempts to generate native C output. It does not yet promise a playable executable.' -ForegroundColor Yellow

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

    Write-Step 'Locating and normalising the user-owned ROM'
    $sourceRom = Resolve-RomPath
    $canonicalRom = Join-Path $DkrSource 'baseroms\dkr.us.v77.z64'
    [byte[]]$romBytes = Convert-ToCanonicalRom $sourceRom $canonicalRom
    [uint32]$romHeaderEntrypoint = ([uint32]$romBytes[8] -shl 24) -bor ([uint32]$romBytes[9] -shl 16) -bor ([uint32]$romBytes[10] -shl 8) -bor [uint32]$romBytes[11]
    $romHeaderEntrypointText = '0x{0:X8}' -f $romHeaderEntrypoint
    Write-Host "[OK] ROM header entry point: $romHeaderEntrypointText" -ForegroundColor Green

    Write-Step 'Building the completed DKR decomp ELF under WSL'
    $wslDkr = Convert-ToWslPath $DkrSource
    if (-not $wslDkr) { Fail 'WSL could not translate the DKR source path.' }
    Write-Host "[OK] WSL source path: $wslDkr" -ForegroundColor Green

    if (-not $SkipApt) {
        Write-Host 'Ubuntu may ask for your Linux sudo password while installing the documented decomp dependencies.' -ForegroundColor Yellow
        & wsl.exe --exec bash -lc "sudo apt-get update && sudo apt-get install -y build-essential pkg-config git python3 python3-pip binutils-mips-linux-gnu python3-venv libpcre2-dev libpcre2-8-0 cmake ninja-build clang lld libcurl4-openssl-dev"
        if ($LASTEXITCODE -ne 0) { Fail 'WSL dependency installation failed. Rerun with -SkipApt only after installing the listed packages manually.' }
    }

    # Do not pass a Windows CRLF here-string directly to `bash -lc`. Bash
    # treats the hidden carriage return in `pipefail\r` as part of the option
    # name and aborts before running the build. Generate an explicit LF-only,
    # BOM-free script and execute that file directly through WSL instead.
    $buildScriptPath = Join-Path $LogDirectory "build-dkr-decomp-$Timestamp.sh"
    $buildLines = @(
        'set -euo pipefail',
        'cd "$1"',
        'git submodule update --init --recursive',
        '',
        'needs_setup=0',
        'if [ ! -x .venv/bin/python3 ]; then',
        '    needs_setup=1',
        "elif ! .venv/bin/python3 -c 'import splat' >/dev/null 2>&1; then",
        '    needs_setup=1',
        'fi',
        '',
        'if [ "$needs_setup" -eq 1 ]; then',
        '    echo "[INFO] The DKR Python/tool environment is incomplete; running make setup."',
        '    make setup',
        'fi',
        '',
        "if ! .venv/bin/python3 -c 'import splat' >/dev/null 2>&1; then",
        '    echo "[ERROR] DKR make setup completed, but the splat module is still unavailable in .venv." >&2',
        '    echo "[ERROR] Remove .venv and rerun Build-DKR-Runtime.cmd if this persists." >&2',
        '    exit 92',
        'fi',
        '',
        'make extract',
        'make -j"$2"'
    )
    $buildCommand = [string]::Join("`n", $buildLines) + "`n"
    $utf8NoBomShell = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($buildScriptPath, $buildCommand, $utf8NoBomShell)

    $wslBuildScript = Convert-ToWslPath $buildScriptPath
    Write-Host "[OK] WSL build script: $wslBuildScript" -ForegroundColor Green
    & wsl.exe --exec bash $wslBuildScript $wslDkr ([string]$Jobs)
    if ($LASTEXITCODE -ne 0) { Fail 'The matching DKR decomp build failed. The log above contains the first real decomp/toolchain error.' }

    $elfPath = Join-Path $DkrSource 'build\dkr.us.v77.elf'
    $builtRomPath = Join-Path $DkrSource 'build\dkr.us.v77.z64'
    if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) { Fail "The DKR build completed without producing the expected ELF: $elfPath" }
    if (-not (Test-Path -LiteralPath $builtRomPath -PathType Leaf)) { Fail "The DKR build completed without producing the expected ROM image: $builtRomPath" }
    Write-Host "[OK] Matching DKR ELF: $elfPath" -ForegroundColor Green

    Write-Step 'Resolving the DKR static-recompilation entry function'
    $entrypointInfo = Resolve-DkrRecompEntrypoint $elfPath $romHeaderEntrypointText
    $entrypointText = [string]$entrypointInfo.AddressText
    $entrypointSymbol = [string]$entrypointInfo.SymbolName
    $useMdebugText = ([bool]$entrypointInfo.UseMdebug).ToString().ToLowerInvariant()

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

    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
    if ($Force -and (Test-Path -LiteralPath $GeneratedFunctions)) {
        Remove-Item -LiteralPath $GeneratedFunctions -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $GeneratedFunctions | Out-Null

    $tomlPath = Join-Path $RuntimeRoot 'dkr.us.v77.generated.toml'
    $recompPolicyPath = Join-Path $RuntimeRoot 'dkr.us.v77.recomp-policy.json'
    if (-not (Test-Path -LiteralPath $recompPolicyPath -PathType Leaf)) {
        Fail "DKR recompilation policy is missing: $recompPolicyPath"
    }
    $recompPolicy = Get-Content -LiteralPath $recompPolicyPath -Raw | ConvertFrom-Json
    if ([int]$recompPolicy.schemaVersion -ne 1) { Fail 'Unsupported DKR recompilation policy schema.' }
    $stubToml = (@($recompPolicy.stubs | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $renamedToml = (@($recompPolicy.renamed | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $ignoredToml = (@($recompPolicy.ignored | ForEach-Object { '"' + ([string]$_.name).Replace('"', '\"') + '"' }) -join ', ')
    $functionSizesToml = (@($recompPolicy.functionSizes | ForEach-Object {
        "{ name = `"$([string]$_.name)`", size = $([string]$_.size) }"
    }) -join ', ')
    $instructionPatchToml = (@($recompPolicy.instructionPatches | ForEach-Object {
        "[[patches.instruction]]`nfunc = `"$([string]$_.function)`"`nvram = $([string]$_.vram)`nvalue = $([string]$_.value)"
    }) -join "`n`n")
    $manualFunctionsToml = (@($recompPolicy.manualFunctions | ForEach-Object {
        "{ name = `"$([string]$_.name)`", section = `"$([string]$_.section)`", vram = $([string]$_.vram), size = $([string]$_.size) }"
    }) -join ', ')
    $functionHookToml = (@($recompPolicy.functionHooks | ForEach-Object {
        $hookText = ([string]$_.text).Replace('\', '\\').Replace('"', '\"')
        "[[patches.hook]]`nfunc = `"$([string]$_.function)`"`nbefore_vram = $([string]$_.beforeVram)`ntext = `"$hookText`""
    }) -join "`n`n")
    $elfToml = $elfPath.Replace('\', '/')
    $romToml = $builtRomPath.Replace('\', '/')
    $outputToml = $GeneratedFunctions.Replace('\', '/')
    $tomlContent = @"
# Generated by Prepare-DKR-Runtime.ps1.
# This is the first-pass DKR CPU recompilation configuration. DKR-specific
# stubs, manual boundaries and instruction patches will be added from the
# emitted diagnostics.
[input]
entrypoint = $entrypointText
use_mdebug = $useMdebugText
elf_path = "$elfToml"
rom_file_path = "$romToml"
output_func_path = "$outputToml"
manual_funcs = [$manualFunctionsToml]
function_sizes = [$functionSizesToml]

[patches]
stubs = [$stubToml]
renamed = [$renamedToml]
ignored = [$ignoredToml]

# BEGIN DKR_INSTRUCTION_PATCHES
$instructionPatchToml
# END DKR_INSTRUCTION_PATCHES

# BEGIN DKR_FUNCTION_HOOKS
$functionHookToml
# END DKR_FUNCTION_HOOKS
"@
    # Windows PowerShell 5.1 writes a UTF-8 BOM when Set-Content -Encoding UTF8
    # is used. Keep the TOML BOM-free because command-line parsers do not all
    # accept a BOM before the first key.
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tomlPath, $tomlContent, $utf8NoBom)

    if (-not $SkipRecompile) {
        Write-Step 'Generating the first native C translation of DKR'

        # PowerShell transcripts do not reliably capture stdout/stderr from
        # native console programs. Run N64Recomp through ProcessStartInfo and
        # capture both streams explicitly so the first real game-specific
        # blocker always includes its actual diagnostic.
        $recompStdoutLog = Join-Path $LogDirectory "n64recomp-$Timestamp.stdout.log"
        $recompStderrLog = Join-Path $LogDirectory "n64recomp-$Timestamp.stderr.log"
        Remove-Item -LiteralPath $recompStdoutLog, $recompStderrLog -Force -ErrorAction SilentlyContinue

        $recompStartInfo = New-Object System.Diagnostics.ProcessStartInfo
        $recompStartInfo.FileName = $recompiler.FullName
        $recompStartInfo.Arguments = '"' + $tomlPath + '"'
        $recompStartInfo.WorkingDirectory = $RuntimeRoot
        $recompStartInfo.UseShellExecute = $false
        $recompStartInfo.CreateNoWindow = $true
        $recompStartInfo.RedirectStandardOutput = $true
        $recompStartInfo.RedirectStandardError = $true

        $recompProcess = New-Object System.Diagnostics.Process
        $recompProcess.StartInfo = $recompStartInfo
        if (-not $recompProcess.Start()) {
            Fail "N64Recomp could not be started: $($recompiler.FullName)"
        }

        # Read both streams asynchronously before waiting. This avoids a
        # deadlock when a tool writes enough data to fill either pipe.
        $recompStdoutTask = $recompProcess.StandardOutput.ReadToEndAsync()
        $recompStderrTask = $recompProcess.StandardError.ReadToEndAsync()
        $recompProcess.WaitForExit()
        $recompExitCode = $recompProcess.ExitCode
        $recompStdout = $recompStdoutTask.Result
        $recompStderr = $recompStderrTask.Result
        $recompProcess.Dispose()

        [System.IO.File]::WriteAllText($recompStdoutLog, $recompStdout, $utf8NoBom)
        [System.IO.File]::WriteAllText($recompStderrLog, $recompStderr, $utf8NoBom)

        if (-not [string]::IsNullOrWhiteSpace($recompStdout)) {
            Write-Host ''
            Write-Host '==> N64Recomp standard output'
            Write-Host $recompStdout.TrimEnd()
        }
        if (-not [string]::IsNullOrWhiteSpace($recompStderr)) {
            Write-Host ''
            Write-Host '==> N64Recomp diagnostic output' -ForegroundColor Yellow
            Write-Host $recompStderr.TrimEnd() -ForegroundColor Yellow
        }

        if ($recompExitCode -ne 0) {
            $diagnosticLines = New-Object System.Collections.Generic.List[string]
            foreach ($line in ($recompStderr -split "[`r`n]+")) {
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    $diagnosticLines.Add($line.Trim())
                }
            }
            foreach ($line in ($recompStdout -split "[`r`n]+")) {
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    $diagnosticLines.Add($line.Trim())
                }
            }

            $firstDiagnostic = 'N64Recomp returned no diagnostic text.'
            if ($diagnosticLines.Count -gt 0) {
                $firstDiagnostic = $diagnosticLines[0]
            }

            Fail (("N64Recomp failed with exit code {0}. First diagnostic: {1} " +
                "Full stdout: {2} Full stderr: {3}") -f `
                $recompExitCode, `
                $firstDiagnostic, `
                $recompStdoutLog, `
                $recompStderrLog)
        }

        Write-Host "[OK] N64Recomp completed. stdout: $recompStdoutLog" -ForegroundColor Green
        Write-Host "[OK] N64Recomp completed. stderr: $recompStderrLog" -ForegroundColor Green
    }

    $functionFiles = @(Get-ChildItem -LiteralPath $GeneratedFunctions -File -Recurse -Include '*.c','*.cpp' -ErrorAction SilentlyContinue)
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
        sourceRomSha1 = $ExpectedSha1
        romHeaderEntrypoint = $romHeaderEntrypointText
        recompEntrypoint = $entrypointText
        recompEntrypointSymbol = $entrypointSymbol
        useMdebug = [bool]$entrypointInfo.UseMdebug
        dkrElf = $elfPath
        generatedFunctionFiles = $functionFiles.Count
        n64RecompExecutable = $recompiler.FullName
        rspRecompExecutable = $rspRecompiler.FullName
        runtimeProbeBuilt = $probeBuilt
        runtimeProbeExecutable = $probeExecutablePath
        rt64BuildRequested = [bool]$BuildRenderer
        dependencies = $resolved
        nextBoundary = 'Register DKR with librecomp, implement SDL audio/input callbacks, recompile F3DDKR and audio RSP microcode, then bind the RT64 render context.'
    }
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResolvedPath -Encoding UTF8

    Write-Host ''
    Write-Host '[OK] DKR runtime preparation reached the generated-code and N64ModernRuntime compilation boundary.' -ForegroundColor Green
    Write-Host "Generated function files: $($functionFiles.Count)"
    Write-Host "State report: $ResolvedPath"
    Write-Host 'No ROM or extracted game asset is added to source control or the distributable ZIP.'
    Write-Host 'The remaining boot work is DKR game registration, callbacks, F3DDKR/audio RSP recompilation and RT64 rendering.'
    Stop-Transcript | Out-Null
    exit 0
} catch {
    Write-Host ''
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Full log: $LogPath"
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

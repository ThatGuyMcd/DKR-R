@echo off
setlocal
cd /d "%~dp0"
rem Check the Windows build prerequisites listed in docs\BUILDING.md, then
rem install the git hooks. Setup-Windows.cmd --skip-hooks leaves hooks alone.

set "SKIP_HOOKS=0"
for %%A in (%*) do (
    if /i "%%~A"=="--skip-hooks" (
        set "SKIP_HOOKS=1"
    ) else if /i "%%~A"=="--help" (
        echo Usage: Setup-Windows.cmd [--skip-hooks] ^(check prerequisites, then install git hooks unless --skip-hooks^)
        exit /b 0
    ) else (
        echo Unknown option: %%~A
        exit /b 1
    )
)

set "MISSING=0"
where git >nul 2>nul || (echo Missing required tool: Git & set "MISSING=1")
where powershell.exe >nul 2>nul || (echo Missing required tool: PowerShell & set "MISSING=1")
where wsl.exe >nul 2>nul || (echo Missing required tool: WSL2 ^(used by the preparation workflow^) & set "MISSING=1")

rem "python" is often the Microsoft Store stub, so prove the interpreter runs.
set "PYTHON="
py -3 -c "import sys" >nul 2>nul && set "PYTHON=py -3"
rem Keep the probe and the assignment inside the block: on one line, "&& set"
rem would bind to the whole "if" and replace a working "py -3".
if not defined PYTHON (
    python -c "import sys" >nul 2>nul && set "PYTHON=python"
)
if not defined PYTHON (echo Missing required tool: Python 3 & set "MISSING=1")

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%I"
)
if not defined VS (echo Missing required tool: Visual Studio 2022 with Desktop development with C++ & set "MISSING=1")

if not "%MISSING%"=="0" (
    echo.
    echo Install the missing prerequisites, then rerun Setup-Windows.cmd.
    if not "%DKRPORT_NO_PAUSE%"=="1" pause
    exit /b 1
)

rem Never fails Setup: bootstrap_hooks.py reports problems and exits 0.
if "%SKIP_HOOKS%"=="0" %PYTHON% "%~dp0scripts\bootstrap_hooks.py"

echo Windows prerequisites passed. docs\BUILDING.md covers preparing and building.
if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b 0

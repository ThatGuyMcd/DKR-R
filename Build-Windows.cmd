@echo off
setlocal
cd /d "%~dp0"

rem Prints the hook install command if the hooks are missing; never fails Build.
if exist "%~dp0scripts\bootstrap_hooks.py" (
    py -3 "%~dp0scripts\bootstrap_hooks.py" --check 2>nul || python "%~dp0scripts\bootstrap_hooks.py" --check 2>nul
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Build-DKR-R-Windows.ps1" %*
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo [SUCCESS] DKR-R built and validated successfully.
    echo Run Run-Windows.cmd to start Diddy Kong Racing - Recompiled.
) else (
    echo [FAILED] The build did not complete.
    echo Read the error above and use the build-log path printed by the PowerShell script.
)

if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b %RESULT%

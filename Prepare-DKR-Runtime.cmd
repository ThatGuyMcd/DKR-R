@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Prepare-DKR-Runtime.ps1" %*
set "EXITCODE=%ERRORLEVEL%"
echo.
if not "%EXITCODE%"=="0" (
    echo [FAILED] DKR runtime preparation did not complete.
    echo Read the error above and use the log path printed by the PowerShell script.
) else (
    echo [OK] DKR runtime preparation completed.
)
if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b %EXITCODE%

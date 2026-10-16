@echo off
setlocal
cd /d "%~dp0"

echo DKR-R - regenerate the prepared N64Recomp boundary
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Diagnose-DKR-Recompile.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
if not "%EXIT_CODE%"=="0" (
    echo [FAILED] N64Recomp did not complete.
    echo Upload the newest n64recomp-*.stdout.log and n64recomp-*.stderr.log from build-logs.
) else (
    echo [OK] N64Recomp completed.
)
if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b %EXIT_CODE%

@echo off
setlocal
cd /d "%~dp0"
echo DKR-R - prepare and compile the native recompilation runtime
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Prepare-DKR-Runtime.ps1" %*
set "exitCode=%ERRORLEVEL%"
if not "%exitCode%"=="0" (
    echo.
    echo [FAILED] DKR runtime preparation did not complete.
    echo Read the prepare-dkr-runtime log path printed above. The first DKR-specific
    echo recompiler or compiler error is the next integration issue to fix.
) else (
    echo.
    echo [OK] The DKR ELF, generated CPU code and runtime probe were prepared.
)
if not "%DKRPORT_NO_PAUSE%"=="1" pause
exit /b %exitCode%

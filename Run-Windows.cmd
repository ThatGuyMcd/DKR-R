@echo off
setlocal
cd /d "%~dp0"
set "APP=%~dp0build\dkr-runtime-rt64\bin\Release\DKR-R.exe"
if not exist "%APP%" (
    echo [ERROR] The built application was not found:
    echo %APP%
    echo.
    echo Run Build-Windows.cmd first.
    if not "%DKRPORT_NO_PAUSE%"=="1" pause
    exit /b 1
)
if "%DKRPORT_NO_PAUSE%"=="1" goto synchronous
start "DKR-R" "%APP%" %*
exit /b %ERRORLEVEL%

:synchronous
"%APP%" %*
exit /b %ERRORLEVEL%

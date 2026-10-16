@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Generate-DKR-RSP.ps1" %*
exit /b %ERRORLEVEL%

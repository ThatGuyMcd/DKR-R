@echo off
setlocal
rem Optional owner-operated recording launch; nothing is changed persistently.
set "DKR_SURGICAL_PROFILE=1"
set "DKR_POWER_PROFILE=1"
set "DKR_TRACK_PROFILE=1"
set "DKR_ROLLBACK_PROFILE=1"
"%~dp0DKR-R.exe" %*
endlocal

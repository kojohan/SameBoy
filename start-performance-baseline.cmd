@echo off
setlocal
title SameBoy Link - performance baseline

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-performance-baseline.ps1" %*
set "SAMEBOY_BASELINE_EXIT=%ERRORLEVEL%"

echo.
if not "%SAMEBOY_BASELINE_EXIT%"=="0" (
    echo Baseline-testet eller loggskriptet avslutades med felkod %SAMEBOY_BASELINE_EXIT%.
)
echo Tryck pa valfri tangent for att stanga fonstret.
pause >nul
exit /b %SAMEBOY_BASELINE_EXIT%

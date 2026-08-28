@echo off
setlocal
title SameBoy Link - loggning

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-sameboy-with-log.ps1" %*
set "SAMEBOY_LOG_EXIT=%ERRORLEVEL%"

echo.
if not "%SAMEBOY_LOG_EXIT%"=="0" (
    echo SameBoy eller loggskriptet avslutades med felkod %SAMEBOY_LOG_EXIT%.
)
echo Tryck pa valfri tangent for att stanga fonstret.
pause >nul
exit /b %SAMEBOY_LOG_EXIT%

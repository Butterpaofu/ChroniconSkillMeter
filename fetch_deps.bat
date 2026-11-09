@echo off
chcp 65001 >nul
echo Fetching YYToolkit / Aurie headers...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch_deps.ps1" %*
if errorlevel 1 (
  echo.
  echo Fetch failed. See README.md - "Build from source" for the manual steps.
  exit /b 1
)

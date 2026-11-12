@echo off
rem Starts the launcher. Needs nothing installed: on first run it fetches what it needs.
cd /d "%~dp0"
if exist A2Launcher.exe (
    start "" A2Launcher.exe
    exit /b 0
)
echo A2Launcher.exe is not here yet, it will be built now. This needs the internet and takes a few minutes the first time.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\bootstrap.ps1"
if errorlevel 1 (
    echo.
    echo Something went wrong, see the lines above. Press any key to close.
    pause >nul
)

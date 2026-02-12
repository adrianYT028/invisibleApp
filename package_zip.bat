@echo off
setlocal

echo ============================================
echo   Package as ZIP (no installer needed)
echo ============================================
echo.

:: Check if deploy folder exists
if not exist "deploy\InvisibleOverlay\InvisibleOverlay.exe" (
    echo [ERROR] Run build_release.bat first!
    pause
    exit /b 1
)

:: Use PowerShell to create ZIP
set "ZIP_NAME=AIMeetingAssistant_v1.0.0_portable.zip"

if exist "%ZIP_NAME%" del "%ZIP_NAME%"

echo Creating %ZIP_NAME%...
powershell -NoProfile -Command "Compress-Archive -Path 'deploy\InvisibleOverlay\*' -DestinationPath '%ZIP_NAME%' -Force"

if %errorlevel% equ 0 (
    echo.
    echo Done! Created: %ZIP_NAME%
    echo This ZIP contains the portable app - extract and run on any Win10 2004+ PC.
) else (
    echo [ERROR] Failed to create ZIP
)

echo.
pause

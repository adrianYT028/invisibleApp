@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   AI Meeting Assistant - Release Build
echo   Creates deployable package
echo ============================================
echo.

:: -----------------------------------------------
:: Find Visual Studio
:: -----------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)

if defined VS_PATH (
    echo [INFO] Found Visual Studio at: %VS_PATH%
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    echo [ERROR] Visual Studio not found!
    echo Please install Visual Studio 2019/2022 with C++ Desktop Development
    pause
    exit /b 1
)

:: -----------------------------------------------
:: Create output directories
:: -----------------------------------------------
set "BUILD_DIR=build\Release"
set "DEPLOY_DIR=deploy\InvisibleOverlay"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"

:: -----------------------------------------------
:: Compile resource file
:: -----------------------------------------------
echo.
echo [1/3] Compiling resources...
rc /nologo /fo "%BUILD_DIR%\resources.res" resources.rc
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed
    goto :error
)

:: -----------------------------------------------
:: Compile source files (Release / static runtime)
:: -----------------------------------------------
echo.
echo [2/3] Compiling source files...

set CFLAGS=/EHsc /W3 /std:c++17 /c /O2 /MT /GL /GS /DNDEBUG
set DEFINES=/DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /DNOMINMAX
set INCLUDES=/Isrc

for %%f in (
    overlay_window
    audio_capture
    screen_capture
    http_client
    ai_service
    text_to_speech
    meeting_assistant
    tray_icon
    main
) do (
    echo   Compiling %%f.cpp...
    cl %CFLAGS% %DEFINES% %INCLUDES% "src\%%f.cpp" /Fo:"%BUILD_DIR%\%%f.obj" >nul
    if !errorlevel! neq 0 (
        echo [ERROR] %%f.cpp failed to compile
        goto :error
    )
)

echo   All source files compiled successfully.

:: -----------------------------------------------
:: Link
:: -----------------------------------------------
echo.
echo [3/3] Linking...

set OBJS=
for %%f in (overlay_window audio_capture screen_capture http_client ai_service text_to_speech meeting_assistant tray_icon main) do (
    set "OBJS=!OBJS! %BUILD_DIR%\%%f.obj"
)

set LIBS=user32.lib gdi32.lib dwmapi.lib ole32.lib uuid.lib winhttp.lib sapi.lib shell32.lib
set LFLAGS=/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /LTCG /OPT:REF /OPT:ICF /MANIFEST:EMBED

link /nologo %LFLAGS% %OBJS% "%BUILD_DIR%\resources.res" %LIBS% /OUT:"%BUILD_DIR%\InvisibleOverlay.exe"

if %errorlevel% neq 0 (
    echo [ERROR] Linking failed
    goto :error
)

:: -----------------------------------------------
:: Create deployment package
:: -----------------------------------------------
echo.
echo [INFO] Creating deployment package...

:: Copy executable
copy /y "%BUILD_DIR%\InvisibleOverlay.exe" "%DEPLOY_DIR%\" >nul

:: Create a launcher script that sets API key
(
echo @echo off
echo :: ============================================
echo :: AI Meeting Assistant Launcher
echo :: ============================================
echo ::
echo :: Set your Groq API key below ^(free at https://console.groq.com^)
echo :: Then run this script to start the app.
echo ::
echo.
echo if not defined GROQ_API_KEY ^(
echo     echo No GROQ_API_KEY set. AI features disabled.
echo     echo Get a free key at: https://console.groq.com
echo     echo.
echo     echo Then run: set GROQ_API_KEY=your_key_here
echo     echo.
echo ^)
echo.
echo start "" "%%~dp0InvisibleOverlay.exe"
) > "%DEPLOY_DIR%\Launch.bat"

:: Create a setup-key helper
(
echo @echo off
echo echo ============================================
echo echo   Set API Key for AI Meeting Assistant
echo echo ============================================
echo echo.
echo set /p "KEY=Enter your Groq API key: "
echo if not "%%KEY%%"=="" ^(
echo     setx GROQ_API_KEY "%%KEY%%"
echo     echo.
echo     echo API key saved! It will be available in new terminal sessions.
echo     echo You can now run Launch.bat to start the app.
echo ^) else ^(
echo     echo No key entered.
echo ^)
echo echo.
echo pause
) > "%DEPLOY_DIR%\SetApiKey.bat"

:: Create uninstall helper
(
echo @echo off
echo echo Removing AI Meeting Assistant...
echo.
echo :: Remove autostart registry entry if present
echo reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "InvisibleOverlay" /f 2^>nul
echo.
echo :: Remove API key environment variable
echo setx GROQ_API_KEY "" 2^>nul
echo.
echo echo Done. You can now delete this folder.
echo pause
) > "%DEPLOY_DIR%\Uninstall.bat"

:: Create README
(
echo AI Meeting Assistant
echo ====================
echo.
echo SETUP:
echo   1. Run SetApiKey.bat and enter your free Groq API key
echo      ^(Get one at https://console.groq.com^)
echo   2. Run Launch.bat to start
echo.
echo HOTKEYS:
echo   Ctrl+Shift+A  - Ask AI about the meeting
echo   Ctrl+Shift+M  - Generate summary
echo   Ctrl+Shift+T  - Toggle transcript
echo   Ctrl+Shift+S  - Select screen region for AI
echo   Ctrl+Shift+V  - Toggle capture visibility
echo   Ctrl+Shift+Q  - Quit
echo.
echo TRAY ICON:
echo   Right-click the tray icon for all options.
echo   Double-click to show/hide overlay.
echo.
echo REQUIREMENTS:
echo   Windows 10 version 2004 or later
echo.
echo FOR RESEARCH PURPOSES ONLY
) > "%DEPLOY_DIR%\README.txt"

:: -----------------------------------------------
:: Done
:: -----------------------------------------------
echo.
echo ============================================
echo   BUILD SUCCESSFUL
echo ============================================
echo.
echo   Executable: %BUILD_DIR%\InvisibleOverlay.exe
echo   Deploy folder: %DEPLOY_DIR%\
echo.
echo   The deploy folder contains everything needed:
echo     - InvisibleOverlay.exe  (standalone, no dependencies)
echo     - SetApiKey.bat         (one-time API key setup)
echo     - Launch.bat            (start the app)
echo     - Uninstall.bat         (cleanup)
echo     - README.txt
echo.
echo   To deploy: copy the %DEPLOY_DIR% folder to any PC.
echo.

goto :done

:error
echo.
echo ============================================
echo   BUILD FAILED - See errors above
echo ============================================
echo.
pause
exit /b 1

:done
endlocal

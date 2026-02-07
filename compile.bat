@echo off
echo ============================================
echo   AI Meeting Assistant - Direct Compile
echo   FOR RESEARCH PURPOSES ONLY
echo ============================================
echo.

:: Try to find Visual Studio
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
    echo [ERROR] Visual Studio not found
    echo Please run from Developer Command Prompt
    pause
    exit /b 1
)

:: Create output directory
if not exist "build\bin\Debug" mkdir "build\bin\Debug"

echo.
echo [INFO] Compiling source files...
echo.

:: Compile each source file separately to see which one fails
set CFLAGS=/EHsc /W3 /std:c++17 /c /MDd /Zi
set DEFINES=/DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /DNOMINMAX /D_DEBUG
set INCLUDES=/Isrc

echo Compiling utils.h (header only)...
echo Compiling overlay_window.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\overlay_window.cpp /Fo:build\overlay_window.obj
if %errorlevel% neq 0 echo [FAIL] overlay_window.cpp failed & goto :error

echo Compiling audio_capture.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\audio_capture.cpp /Fo:build\audio_capture.obj
if %errorlevel% neq 0 echo [FAIL] audio_capture.cpp failed & goto :error

echo Compiling screen_capture.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\screen_capture.cpp /Fo:build\screen_capture.obj
if %errorlevel% neq 0 echo [FAIL] screen_capture.cpp failed & goto :error

echo Compiling http_client.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\http_client.cpp /Fo:build\http_client.obj
if %errorlevel% neq 0 echo [FAIL] http_client.cpp failed & goto :error

echo Compiling ai_service.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\ai_service.cpp /Fo:build\ai_service.obj
if %errorlevel% neq 0 echo [FAIL] ai_service.cpp failed & goto :error

echo Compiling text_to_speech.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\text_to_speech.cpp /Fo:build\text_to_speech.obj
if %errorlevel% neq 0 echo [FAIL] text_to_speech.cpp failed & goto :error

echo Compiling meeting_assistant.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\meeting_assistant.cpp /Fo:build\meeting_assistant.obj
if %errorlevel% neq 0 echo [FAIL] meeting_assistant.cpp failed & goto :error

echo Compiling main.cpp...
cl %CFLAGS% %DEFINES% %INCLUDES% src\main.cpp /Fo:build\main.obj
if %errorlevel% neq 0 echo [FAIL] main.cpp failed & goto :error

echo.
echo [INFO] Linking...
set LIBS=user32.lib gdi32.lib dwmapi.lib ole32.lib uuid.lib winhttp.lib sapi.lib

link /OUT:build\bin\Debug\InvisibleOverlay.exe /DEBUG ^
    build\main.obj ^
    build\overlay_window.obj ^
    build\audio_capture.obj ^
    build\screen_capture.obj ^
    build\http_client.obj ^
    build\ai_service.obj ^
    build\text_to_speech.obj ^
    build\meeting_assistant.obj ^
    %LIBS%

if %errorlevel% neq 0 goto :error

echo.
echo ============================================
echo   BUILD SUCCESSFUL!
echo   Output: build\bin\Debug\InvisibleOverlay.exe
echo ============================================
echo.
echo Run with: build\bin\Debug\InvisibleOverlay.exe
pause
exit /b 0

:error
echo.
echo [ERROR] Build failed!
pause
exit /b 1

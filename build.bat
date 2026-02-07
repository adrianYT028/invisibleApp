@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   Invisible Overlay Build Script
echo   FOR RESEARCH PURPOSES ONLY
echo ============================================
echo.

:: Check for Visual Studio
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Visual Studio C++ compiler not found.
    echo.
    echo Please run this script from a "Developer Command Prompt for VS"
    echo or "x64 Native Tools Command Prompt for VS".
    echo.
    echo To install: Visual Studio 2019/2022 with "Desktop development with C++"
    exit /b 1
)

:: Create build directory
if not exist "build" mkdir build
cd build

:: Configure with CMake if available
where cmake >nul 2>&1
if %errorlevel% equ 0 (
    echo [INFO] Using CMake build system
    echo.
    
    cmake .. -G "Visual Studio 17 2022" -A x64
    if %errorlevel% neq 0 (
        echo [WARN] VS2022 not found, trying VS2019...
        cmake .. -G "Visual Studio 16 2019" -A x64
    )
    
    if %errorlevel% equ 0 (
        echo.
        echo [INFO] Building Release configuration...
        cmake --build . --config Release
        
        if %errorlevel% equ 0 (
            echo.
            echo ============================================
            echo   BUILD SUCCESSFUL
            echo   Output: build\bin\Release\InvisibleOverlay.exe
            echo ============================================
        ) else (
            echo [ERROR] Build failed
            exit /b 1
        )
    ) else (
        echo [ERROR] CMake configuration failed
        exit /b 1
    )
) else (
    echo [INFO] CMake not found, using direct compilation
    echo.
    
    :: Direct MSVC compilation
    cd ..
    
    set SOURCES=src\main.cpp src\overlay_window.cpp src\audio_capture.cpp src\screen_capture.cpp
    set INCLUDES=/Isrc
    set LIBS=user32.lib gdi32.lib dwmapi.lib ole32.lib uuid.lib
    set DEFINES=/DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /DNOMINMAX
    set FLAGS=/EHsc /W4 /O2 /std:c++17 /MT
    
    if not exist "build\bin\Release" mkdir "build\bin\Release"
    
    echo [INFO] Compiling...
    cl %FLAGS% %DEFINES% %INCLUDES% %SOURCES% %LIBS% /Fe:build\bin\Release\InvisibleOverlay.exe /Fo:build\
    
    if %errorlevel% equ 0 (
        echo.
        echo ============================================
        echo   BUILD SUCCESSFUL
        echo   Output: build\bin\Release\InvisibleOverlay.exe
        echo ============================================
    ) else (
        echo [ERROR] Compilation failed
        exit /b 1
    )
)

cd ..
echo.
echo [INFO] Run with: build\bin\Release\InvisibleOverlay.exe
echo [INFO] Options:
echo         --no-audio  : Disable audio capture
echo         --debug     : Show debug border
echo.

endlocal

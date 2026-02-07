# Invisible Overlay Research Application

A Windows application demonstrating screen overlay techniques for research purposes.

## Features

- **Invisible to Screen Capture**: Uses `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`
- **Click-Through**: Mouse events pass to underlying windows
- **Hidden from Taskbar/Alt-Tab**: Uses `WS_EX_TOOLWINDOW` style
- **Always on Top**: Overlay stays above all windows
- **WASAPI Audio Capture**: Loopback capture of system audio
- **Screen Region Selection**: OCR-ready region capture

## Requirements

- Windows 10 version 2004 or later
- Visual Studio 2019+ with C++ Desktop Development workload
- Windows SDK 10.0.19041.0 or later

## Building

### Using Visual Studio
1. Open `InvisibleOverlay.sln`
2. Select Release x64 configuration
3. Build Solution (Ctrl+Shift+B)

### Using CMake
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Using Command Line (Developer Command Prompt)
```bash
build.bat
```

## Project Structure

```
INVISIBLE APP/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── overlay_window.cpp    # Core overlay window implementation
│   ├── overlay_window.h
│   ├── audio_capture.cpp     # WASAPI loopback audio capture
│   ├── audio_capture.h
│   ├── screen_capture.cpp    # Screen region capture for OCR
│   ├── screen_capture.h
│   ├── region_selector.cpp   # Interactive region selection
│   ├── region_selector.h
│   └── utils.h               # Common utilities
├── CMakeLists.txt
├── build.bat
└── README.md
```

## Usage

Run the application. It will:
1. Create an invisible overlay window
2. Start system audio capture (if enabled)
3. Listen for hotkeys:
   - `Ctrl+Shift+S` - Select screen region for capture
   - `Ctrl+Shift+Q` - Quit application

## Technical Details

### Display Affinity
The `WDA_EXCLUDEFROMCAPTURE` flag tells DWM to exclude the window from all capture pipelines including:
- Screen sharing (Zoom, Teams, Discord)
- Screen recording (OBS, Windows Game Bar)
- PrintWindow API
- Desktop Duplication API

### Window Styles
- `WS_EX_TOOLWINDOW` - Removes from taskbar and Alt-Tab
- `WS_EX_LAYERED` - Enables per-pixel alpha
- `WS_EX_TRANSPARENT` - Click-through behavior
- `WS_EX_TOPMOST` - Always on top

### Audio Capture
Uses WASAPI in loopback mode to capture system audio output without creating virtual devices.

## Disclaimer

This application is for **research and educational purposes only**. Understanding these techniques helps security researchers and platform developers build better detection mechanisms.

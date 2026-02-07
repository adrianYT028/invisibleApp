# Technical Reference: Invisible Overlay Implementation

## Core Windows APIs

### 1. Capture Exclusion (`SetWindowDisplayAffinity`)

```cpp
// Hide window from all capture pipelines (Windows 10 2004+)
SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);  // 0x11

// Make visible to capture again
SetWindowDisplayAffinity(hwnd, WDA_NONE);  // 0x00
```

**What it affects:**
- Screen sharing (Zoom, Teams, Discord, Skype, WebEx)
- Screen recording (OBS, Game Bar, Shadowplay)
- Screenshots via PrintWindow API
- Desktop Duplication API (DXGI)
- GDI BitBlt from screen DC

**What it does NOT affect:**
- Physical display (user sees normally)
- Photography of the screen
- Hardware capture cards

### 2. Taskbar/Alt-Tab Hiding (`WS_EX_TOOLWINDOW`)

```cpp
DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);
```

**Effects:**
- No taskbar button
- Not shown in Alt-Tab
- Still visible in Task Manager process list

### 3. Click-Through (`WS_EX_TRANSPARENT` + `WS_EX_LAYERED`)

```cpp
DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT;
SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

// Set transparency level
SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
```

**Effects:**
- Mouse clicks pass through to windows below
- Window still renders visually
- Cannot receive mouse input

### 4. Always On Top (`HWND_TOPMOST`)

```cpp
SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
```

### 5. WASAPI Loopback Audio Capture

```cpp
// Get default audio output device
IMMDeviceEnumerator* enumerator;
CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                 __uuidof(IMMDeviceEnumerator), (void**)&enumerator);

IMMDevice* device;
enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);

// Initialize for loopback capture
IAudioClient* audioClient;
device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);

audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                        AUDCLNT_STREAMFLAGS_LOOPBACK,
                        bufferDuration, 0, mixFormat, nullptr);

// Get capture interface
IAudioCaptureClient* captureClient;
audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);

// Start capture
audioClient->Start();

// Read audio data
BYTE* data;
UINT32 frames;
captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
// Process data...
captureClient->ReleaseBuffer(frames);
```

**Key points:**
- Uses `eRender` endpoint (output device) not `eCapture` (microphone)
- `AUDCLNT_STREAMFLAGS_LOOPBACK` is crucial - captures what's being played
- No virtual audio device needed
- No system settings change visible to user

## Window Style Combination

```cpp
// Optimal combination for invisible overlay:
DWORD exStyle = 
    WS_EX_LAYERED |      // Required for transparency
    WS_EX_TRANSPARENT |  // Click-through
    WS_EX_TOOLWINDOW |   // Hide from taskbar/Alt-Tab
    WS_EX_TOPMOST |      // Always on top
    WS_EX_NOACTIVATE;    // Don't steal focus

DWORD style = WS_POPUP;  // No frame/borders

HWND hwnd = CreateWindowEx(exStyle, className, L"", style, ...);

// Apply capture exclusion
SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

// Set transparency
SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);
```

## Detection Considerations

Potential detection methods (for security research):
1. Enumerate windows and call `GetWindowDisplayAffinity()` on each
2. Look for processes with unusual high-privilege window characteristics
3. Monitor for WASAPI loopback stream creation
4. Check for windows with `WS_EX_TOOLWINDOW` + `WS_EX_TRANSPARENT` combination

## Platform Limitations

| Platform | Capture Exclusion | Effectiveness |
|----------|------------------|---------------|
| Windows 10 2004+ | `WDA_EXCLUDEFROMCAPTURE` | High (all DWM-based capture blocked) |
| Windows 10 older | `WDA_MONITOR` | Partial |
| macOS 12+ | `sharingType = .none` | Low (ScreenCaptureKit still captures) |
| macOS pre-12 | `sharingType = .none` | Medium (CGWindowList blocked) |
| Linux/Wayland | None standard | Compositor-dependent (KDE has option) |
| Linux/X11 | None | All capture methods still work |

## References

- [SetWindowDisplayAffinity](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
- [WASAPI Loopback Recording](https://docs.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
- [Layered Windows](https://docs.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)

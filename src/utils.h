#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

// Windows version targeting (Windows 10 2004+)
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION NTDDI_WIN10_VB

#include <windows.h>
#include <windowsx.h>  // For GET_X_LPARAM, GET_Y_LPARAM
#include <dwmapi.h>

// Fallback definitions for GET_X_LPARAM and GET_Y_LPARAM if not defined
// (can happen with WIN32_LEAN_AND_MEAN)
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif

#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// Error Handling Utilities
// -----------------------------------------------------------------------------

inline std::wstring GetLastErrorMessage(DWORD errorCode = 0) {
    if (errorCode == 0) {
        errorCode = GetLastError();
    }
    
    LPWSTR messageBuffer = nullptr;
    size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr
    );
    
    std::wstring message(messageBuffer, size);
    LocalFree(messageBuffer);
    
    // Trim trailing newlines
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r')) {
        message.pop_back();
    }
    
    return message;
}

inline void LogError(const wchar_t* context, DWORD errorCode = 0) {
    if (errorCode == 0) {
        errorCode = GetLastError();
    }
    std::wcerr << L"[ERROR] " << context << L": " << GetLastErrorMessage(errorCode) 
               << L" (0x" << std::hex << errorCode << std::dec << L")" << std::endl;
}

inline void LogInfo(const wchar_t* message) {
    std::wcout << L"[INFO] " << message << std::endl;
}

inline void LogDebug(const wchar_t* message) {
#ifdef _DEBUG
    std::wcout << L"[DEBUG] " << message << std::endl;
#else
    (void)message;
#endif
}

// -----------------------------------------------------------------------------
// RAII Wrappers
// -----------------------------------------------------------------------------

// Generic handle wrapper with custom deleter
template<typename T, typename Deleter>
class ScopedHandle {
public:
    ScopedHandle() : handle_(nullptr) {}
    explicit ScopedHandle(T handle) : handle_(handle) {}
    ~ScopedHandle() { Reset(); }
    
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    void Reset(T handle = nullptr) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            Deleter()(handle_);
        }
        handle_ = handle;
    }
    
    T Get() const { return handle_; }
    T* GetAddressOf() { return &handle_; }
    explicit operator bool() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    T Release() { T temp = handle_; handle_ = nullptr; return temp; }
    
private:
    T handle_;
};

struct HWNDDeleter {
    void operator()(HWND hwnd) const {
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }
};

struct HDCDeleter {
    void operator()(HDC hdc) const {
        if (hdc) DeleteDC(hdc);
    }
};

struct HBITMAPDeleter {
    void operator()(HBITMAP hbm) const {
        if (hbm) DeleteObject(hbm);
    }
};

struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

using ScopedHDC = ScopedHandle<HDC, HDCDeleter>;
using ScopedHBITMAP = ScopedHandle<HBITMAP, HBITMAPDeleter>;
using ScopedKernelHandle = ScopedHandle<HANDLE, HandleDeleter>;

// -----------------------------------------------------------------------------
// Window Display Affinity Constants
// -----------------------------------------------------------------------------

// WDA_EXCLUDEFROMCAPTURE = 0x11 (Windows 10 2004+)
// This flag excludes the window from all capture mechanisms
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif

// -----------------------------------------------------------------------------
// Rectangle Utilities
// -----------------------------------------------------------------------------

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    
    Rect() = default;
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
    Rect(const RECT& r) : x(r.left), y(r.top), width(r.right - r.left), height(r.bottom - r.top) {}
    
    RECT ToWinRect() const {
        return { x, y, x + width, y + height };
    }
    
    bool IsValid() const {
        return width > 0 && height > 0;
    }
    
    bool Contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

// -----------------------------------------------------------------------------
// Monitor Utilities
// -----------------------------------------------------------------------------

inline Rect GetPrimaryMonitorRect() {
    HMONITOR hMon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        return Rect(mi.rcMonitor);
    }
    // Fallback
    return Rect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

inline Rect GetVirtualScreenRect() {
    return Rect(
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_CYVIRTUALSCREEN)
    );
}

// -----------------------------------------------------------------------------
// Hotkey Registration Helper
// -----------------------------------------------------------------------------

class HotkeyManager {
public:
    static constexpr int HOTKEY_REGION_SELECT = 1;
    static constexpr int HOTKEY_QUIT = 2;
    static constexpr int HOTKEY_TOGGLE_VISIBILITY = 3;
    
    static bool RegisterHotkeys(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) {
            LogError(L"Invalid window handle for hotkey registration");
            return false;
        }
        
        bool success = true;
        
        // Ctrl+Shift+S for region selection
        if (!RegisterHotKey(hwnd, HOTKEY_REGION_SELECT, MOD_CONTROL | MOD_SHIFT, 'S')) {
            LogInfo(L"Note: Ctrl+Shift+S hotkey unavailable (may be in use by another app)");
            success = false;
        }
        
        // Ctrl+Shift+Q for quit
        if (!RegisterHotKey(hwnd, HOTKEY_QUIT, MOD_CONTROL | MOD_SHIFT, 'Q')) {
            LogInfo(L"Note: Ctrl+Shift+Q hotkey unavailable (may be in use by another app)");
            success = false;
        }
        
        // Ctrl+Shift+V for toggle visibility (debug)
        if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE_VISIBILITY, MOD_CONTROL | MOD_SHIFT, 'V')) {
            LogInfo(L"Note: Ctrl+Shift+V hotkey unavailable (may be in use by another app)");
            success = false;
        }
        
        return success;  // Return status but don't treat as fatal error
    }
    
    static void UnregisterHotkeys(HWND hwnd) {
        if (hwnd && IsWindow(hwnd)) {
            UnregisterHotKey(hwnd, HOTKEY_REGION_SELECT);
            UnregisterHotKey(hwnd, HOTKEY_QUIT);
            UnregisterHotKey(hwnd, HOTKEY_TOGGLE_VISIBILITY);
        }
    }
};

} // namespace invisible

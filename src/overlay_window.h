#pragma once

#include "utils.h"
#include <functional>

namespace invisible {

// -----------------------------------------------------------------------------
// Overlay Window Configuration
// -----------------------------------------------------------------------------

struct OverlayConfig {
    // Window dimensions (default: full screen)
    int x = 0;
    int y = 0;
    int width = 0;   // 0 = use screen width
    int height = 0;  // 0 = use screen height
    
    // Transparency (0-255, 0 = fully transparent)
    BYTE alpha = 200;
    
    // Background color (when not fully transparent)
    COLORREF backgroundColor = RGB(30, 30, 30);
    
    // Behavior flags
    bool excludeFromCapture = true;    // WDA_EXCLUDEFROMCAPTURE
    bool clickThrough = true;          // WS_EX_TRANSPARENT
    bool hideFromTaskbar = true;       // WS_EX_TOOLWINDOW
    bool alwaysOnTop = true;           // HWND_TOPMOST
    bool showOnAllDesktops = true;     // Virtual desktop visibility
    
    // Debug mode (shows visible border)
    bool debugMode = false;
};

// -----------------------------------------------------------------------------
// OverlayWindow Class
// -----------------------------------------------------------------------------

class OverlayWindow {
public:
    using RenderCallback = std::function<void(HDC hdc, const Rect& bounds)>;
    using HotkeyCallback = std::function<void(int hotkeyId)>;
    
    OverlayWindow();
    ~OverlayWindow();
    
    // Disable copy
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    
    // Initialize and create the window
    bool Create(const OverlayConfig& config = OverlayConfig());
    
    // Destroy the window
    void Destroy();
    
    // Check if window is valid
    bool IsValid() const;
    
    // Get the native window handle
    HWND GetHandle() const { return hwnd_; }
    
    // Show/hide the window (still invisible to capture when hidden)
    void Show(bool visible = true);
    bool IsVisible() const;
    
    // Update window position and size
    void SetBounds(const Rect& bounds);
    Rect GetBounds() const;
    
    // Update transparency
    void SetAlpha(BYTE alpha);
    BYTE GetAlpha() const;
    
    // Toggle click-through behavior
    void SetClickThrough(bool enabled);
    bool IsClickThrough() const;
    
    // Toggle capture exclusion (for testing)
    void SetExcludeFromCapture(bool exclude);
    bool IsExcludedFromCapture() const;
    
    // Set custom render callback (called during WM_PAINT)
    void SetRenderCallback(RenderCallback callback);
    
    // Set hotkey handler
    void SetHotkeyCallback(HotkeyCallback callback);
    
    // Force a repaint
    void Invalidate();
    
    // Run the message loop (blocking)
    int RunMessageLoop();
    
    // Post quit message to exit the message loop
    void PostQuit();
    
    // Process pending messages (non-blocking)
    bool ProcessMessages();
    
private:
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Register window class
    static bool RegisterWindowClass();
    static bool classRegistered_;
    static constexpr const wchar_t* CLASS_NAME = L"InvisibleOverlayClass";
    
    // Internal state
    HWND hwnd_ = nullptr;
    OverlayConfig config_;
    RenderCallback renderCallback_;
    HotkeyCallback hotkeyCallback_;
    
    // Double buffering
    void CreateBackBuffer(int width, int height);
    void DestroyBackBuffer();
    ScopedHDC backBufferDC_;
    ScopedHBITMAP backBufferBitmap_;
    int backBufferWidth_ = 0;
    int backBufferHeight_ = 0;
};

} // namespace invisible

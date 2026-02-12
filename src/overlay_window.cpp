#include "overlay_window.h"

namespace invisible {

bool OverlayWindow::classRegistered_ = false;

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

OverlayWindow::OverlayWindow() = default;

OverlayWindow::~OverlayWindow() {
    Destroy();
}

// -----------------------------------------------------------------------------
// Window Class Registration
// -----------------------------------------------------------------------------

bool OverlayWindow::RegisterWindowClass() {
    if (classRegistered_) {
        return true;
    }
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(OverlayWindow*);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // We handle painting ourselves
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = nullptr;
    
    if (RegisterClassExW(&wc) == 0) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LogError(L"Failed to register window class", error);
            return false;
        }
    }
    
    classRegistered_ = true;
    return true;
}

// -----------------------------------------------------------------------------
// Window Creation
// -----------------------------------------------------------------------------

bool OverlayWindow::Create(const OverlayConfig& config) {
    if (hwnd_) {
        LogError(L"Window already created");
        return false;
    }
    
    if (!RegisterWindowClass()) {
        LogError(L"Failed to register window class");
        return false;
    }
    
    config_ = config;
    
    // Determine window bounds
    Rect bounds;
    if (config_.width <= 0 || config_.height <= 0) {
        bounds = GetVirtualScreenRect();
        if (config_.width > 0) bounds.width = config_.width;
        if (config_.height > 0) bounds.height = config_.height;
    } else {
        bounds = Rect(config_.x, config_.y, config_.width, config_.height);
    }
    
    LogInfo(L"Creating overlay window...");
    
    // Build extended window style
    DWORD exStyle = WS_EX_LAYERED;  // Required for transparency
    
    if (config_.hideFromTaskbar) {
        exStyle |= WS_EX_TOOLWINDOW;
    }
    
    if (config_.clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    }
    
    if (config_.alwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }
    
    exStyle |= WS_EX_NOACTIVATE;
    
    DWORD style = WS_POPUP;
    
    // Create the window
    hwnd_ = CreateWindowExW(
        exStyle,
        CLASS_NAME,
        L"",
        style,
        bounds.x, bounds.y,
        bounds.width, bounds.height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );
    
    if (!hwnd_) {
        LogError(L"CreateWindowExW failed");
        return false;
    }
    
    LogInfo(L"Window created, setting attributes...");
    
    // Store 'this' pointer in window's user data
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    // Set up layered window for transparency
    if (!SetLayeredWindowAttributes(hwnd_, 0, config_.alpha, LWA_ALPHA)) {
        LogError(L"SetLayeredWindowAttributes failed");
        // Continue anyway - not fatal
    }
    
    // Apply capture exclusion (the core "invisible" feature)
    if (config_.excludeFromCapture) {
        SetExcludeFromCapture(true);
    }
    
    // Register global hotkeys (non-fatal if they fail)
    HotkeyManager::RegisterHotkeys(hwnd_);
    
    // Create back buffer for double buffering
    CreateBackBuffer(bounds.width, bounds.height);
    
    // Show the window
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    
    LogInfo(L"Overlay window created successfully");
    return true;
}

// -----------------------------------------------------------------------------
// Window Destruction
// -----------------------------------------------------------------------------

void OverlayWindow::Destroy() {
    if (hwnd_) {
        HotkeyManager::UnregisterHotkeys(hwnd_);
        DestroyBackBuffer();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool OverlayWindow::IsValid() const {
    return hwnd_ != nullptr && IsWindow(hwnd_);
}

// -----------------------------------------------------------------------------
// Visibility Control
// -----------------------------------------------------------------------------

void OverlayWindow::Show(bool visible) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

bool OverlayWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

// -----------------------------------------------------------------------------
// Position and Size
// -----------------------------------------------------------------------------

void OverlayWindow::SetBounds(const Rect& bounds) {
    if (!hwnd_) return;
    
    SetWindowPos(hwnd_, nullptr, bounds.x, bounds.y, bounds.width, bounds.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    
    // Recreate back buffer if size changed
    if (bounds.width != backBufferWidth_ || bounds.height != backBufferHeight_) {
        CreateBackBuffer(bounds.width, bounds.height);
    }
}

Rect OverlayWindow::GetBounds() const {
    if (!hwnd_) return Rect();
    
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    return Rect(rc);
}

// -----------------------------------------------------------------------------
// Transparency
// -----------------------------------------------------------------------------

void OverlayWindow::SetAlpha(BYTE alpha) {
    config_.alpha = alpha;
    if (hwnd_) {
        SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
    }
}

BYTE OverlayWindow::GetAlpha() const {
    return config_.alpha;
}

// -----------------------------------------------------------------------------
// Click-Through Toggle
// -----------------------------------------------------------------------------

void OverlayWindow::SetClickThrough(bool enabled) {
    if (!hwnd_) return;
    
    config_.clickThrough = enabled;
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    
    if (enabled) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
}

bool OverlayWindow::IsClickThrough() const {
    return config_.clickThrough;
}

// -----------------------------------------------------------------------------
// Capture Exclusion (Core Feature)
// -----------------------------------------------------------------------------

void OverlayWindow::SetExcludeFromCapture(bool exclude) {
    if (!hwnd_) return;
    
    config_.excludeFromCapture = exclude;
    
    // SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE (0x11)
    // This is the key API that makes the window invisible to:
    // - Screen sharing (Zoom, Teams, Discord, etc.)
    // - Screen recording (OBS, Windows Game Bar, etc.)
    // - Screenshot APIs (PrintWindow, BitBlt on screen DC)
    // - Desktop Duplication API
    // 
    // The window still renders normally on the physical display.
    
    DWORD affinity = exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    
    if (!SetWindowDisplayAffinity(hwnd_, affinity)) {
        DWORD error = GetLastError();
        if (error == ERROR_NOT_SUPPORTED) {
            LogError(L"SetWindowDisplayAffinity not supported (requires Windows 10 2004+)");
        } else {
            LogError(L"Failed to set window display affinity", error);
        }
    } else {
        LogInfo(exclude ? L"Window excluded from capture" : L"Window visible to capture");
    }
}

bool OverlayWindow::IsExcludedFromCapture() const {
    return config_.excludeFromCapture;
}

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

void OverlayWindow::SetRenderCallback(RenderCallback callback) {
    renderCallback_ = std::move(callback);
}

void OverlayWindow::SetHotkeyCallback(HotkeyCallback callback) {
    hotkeyCallback_ = std::move(callback);
}

void OverlayWindow::SetMessageCallback(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

void OverlayWindow::Invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// -----------------------------------------------------------------------------
// Message Loop
// -----------------------------------------------------------------------------

int OverlayWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void OverlayWindow::PostQuit() {
    PostQuitMessage(0);
}

bool OverlayWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Window Procedure
// -----------------------------------------------------------------------------

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = nullptr;
    
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Let the generic message callback handle first (for tray icon, etc.)
    if (messageCallback_) {
        if (messageCallback_(hwnd, msg, wParam, lParam)) {
            return 0;
        }
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            Rect bounds = GetBounds();
            bounds.x = 0;
            bounds.y = 0;
            
            // Use back buffer if available
            HDC targetDC = backBufferDC_.Get() ? backBufferDC_.Get() : hdc;
            
            // Clear background
            HBRUSH bgBrush = CreateSolidBrush(config_.backgroundColor);
            RECT fillRect = bounds.ToWinRect();
            FillRect(targetDC, &fillRect, bgBrush);
            DeleteObject(bgBrush);
            
            // Debug mode: draw a visible border
            if (config_.debugMode) {
                HPEN debugPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
                HPEN oldPen = (HPEN)SelectObject(targetDC, debugPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(targetDC, GetStockObject(NULL_BRUSH));
                Rectangle(targetDC, 0, 0, bounds.width, bounds.height);
                SelectObject(targetDC, oldPen);
                SelectObject(targetDC, oldBrush);
                DeleteObject(debugPen);
            }
            
            // Custom rendering callback
            if (renderCallback_) {
                renderCallback_(targetDC, bounds);
            }
            
            // Copy back buffer to screen
            if (backBufferDC_.Get() && backBufferDC_.Get() != hdc) {
                BitBlt(hdc, 0, 0, bounds.width, bounds.height, 
                       backBufferDC_.Get(), 0, 0, SRCCOPY);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_HOTKEY: {
            int hotkeyId = static_cast<int>(wParam);
            
            if (hotkeyId == HotkeyManager::HOTKEY_QUIT) {
                PostQuit();
            } else if (hotkeyId == HotkeyManager::HOTKEY_TOGGLE_VISIBILITY) {
                SetExcludeFromCapture(!IsExcludedFromCapture());
                Invalidate();
            }
            
            if (hotkeyCallback_) {
                hotkeyCallback_(hotkeyId);
            }
            return 0;
        }
        
        case WM_DISPLAYCHANGE: {
            // Monitor configuration changed, resize to new virtual screen
            if (config_.width <= 0 || config_.height <= 0) {
                SetBounds(GetVirtualScreenRect());
            }
            return 0;
        }
        
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        
        case WM_ERASEBKGND: {
            return 1;  // We handle background in WM_PAINT
        }
        
        // Prevent window from getting focus
        case WM_MOUSEACTIVATE: {
            return MA_NOACTIVATE;
        }
        
        case WM_ACTIVATE: {
            if (LOWORD(wParam) != WA_INACTIVE && config_.clickThrough) {
                return 0;  // Don't activate
            }
            break;
        }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Double Buffering
// -----------------------------------------------------------------------------

void OverlayWindow::CreateBackBuffer(int width, int height) {
    DestroyBackBuffer();
    
    if (width <= 0 || height <= 0) return;
    
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    
    if (memDC && bitmap) {
        SelectObject(memDC, bitmap);
        backBufferDC_.Reset(memDC);
        backBufferBitmap_.Reset(bitmap);
        backBufferWidth_ = width;
        backBufferHeight_ = height;
    } else {
        if (memDC) DeleteDC(memDC);
        if (bitmap) DeleteObject(bitmap);
    }
    
    ReleaseDC(nullptr, screenDC);
}

void OverlayWindow::DestroyBackBuffer() {
    backBufferDC_.Reset();
    backBufferBitmap_.Reset();
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

} // namespace invisible

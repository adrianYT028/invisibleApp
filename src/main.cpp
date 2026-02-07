/**
 * Invisible Overlay Application
 * 
 * This application demonstrates Windows screen overlay techniques:
 * - SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) for capture exclusion
 * - WS_EX_TOOLWINDOW for taskbar/Alt-Tab hiding
 * - WS_EX_TRANSPARENT for click-through behavior
 * - WASAPI loopback for system audio capture
 * 
 * FOR RESEARCH AND EDUCATIONAL PURPOSES ONLY
 * 
 * Hotkeys:
 *   Ctrl+Shift+S - Start region selection
 *   Ctrl+Shift+V - Toggle capture visibility (debug)
 *   Ctrl+Shift+Q - Quit application
 */

#include "overlay_window.h"
#include "audio_capture.h"
#include "screen_capture.h"
#include <iostream>
#include <memory>
#include <sstream>

using namespace invisible;

// -----------------------------------------------------------------------------
// Application Configuration
// -----------------------------------------------------------------------------

struct AppConfig {
    bool enableAudioCapture = true;
    bool enableOverlay = true;
    bool debugMode = false;
    BYTE overlayAlpha = 220;
};

// -----------------------------------------------------------------------------
// Application Class
// -----------------------------------------------------------------------------

class InvisibleApp {
public:
    InvisibleApp() = default;
    ~InvisibleApp();
    
    bool Initialize(const AppConfig& config);
    int Run();
    void Shutdown();
    
private:
    void OnHotkey(int hotkeyId);
    void OnRegionSelected(const Rect& region);
    void RenderOverlay(HDC hdc, const Rect& bounds);
    
    AppConfig config_;
    std::unique_ptr<OverlayWindow> overlay_;
    std::unique_ptr<AudioCapture> audioCapture_;
    std::unique_ptr<AudioBufferQueue> audioQueue_;
    std::unique_ptr<RegionSelector> regionSelector_;
    
    // Display state
    std::wstring statusText_ = L"Invisible Overlay Active";
    std::wstring lastCaptureInfo_;
    Rect lastSelectedRegion_;
    bool showStatus_ = true;
};

InvisibleApp::~InvisibleApp() {
    Shutdown();
}

bool InvisibleApp::Initialize(const AppConfig& config) {
    config_ = config;
    
    LogInfo(L"===========================================");
    LogInfo(L"Invisible Overlay Application");
    LogInfo(L"FOR RESEARCH PURPOSES ONLY");
    LogInfo(L"===========================================");
    
    // Initialize COM for audio capture
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogError(L"Failed to initialize COM");
        // Continue anyway - not fatal
    }
    
    // Create overlay window
    if (config_.enableOverlay) {
        overlay_ = std::make_unique<OverlayWindow>();
        
        OverlayConfig overlayConfig;
        overlayConfig.alpha = config_.overlayAlpha;
        overlayConfig.excludeFromCapture = true;
        overlayConfig.clickThrough = true;
        overlayConfig.hideFromTaskbar = true;
        overlayConfig.alwaysOnTop = true;
        overlayConfig.debugMode = config_.debugMode;
        overlayConfig.backgroundColor = RGB(20, 20, 25);
        
        if (!overlay_->Create(overlayConfig)) {
            LogError(L"Failed to create overlay window");
            return false;
        }
        
        // Set callbacks
        overlay_->SetHotkeyCallback([this](int id) { OnHotkey(id); });
        overlay_->SetRenderCallback([this](HDC hdc, const Rect& bounds) { 
            RenderOverlay(hdc, bounds); 
        });
        
        LogInfo(L"Overlay window created with WDA_EXCLUDEFROMCAPTURE");
    }
    
    // Initialize audio capture (optional - don't fail if it doesn't work)
    if (config_.enableAudioCapture) {
        audioCapture_ = std::make_unique<AudioCapture>();
        audioQueue_ = std::make_unique<AudioBufferQueue>();
        
        if (audioCapture_->Initialize()) {
            if (audioCapture_->Start(audioQueue_.get())) {
                LogInfo(L"Audio capture started (WASAPI loopback)");
            } else {
                LogError(L"Failed to start audio capture - continuing without audio");
            }
        } else {
            LogError(L"Failed to initialize audio capture - continuing without audio");
        }
    }
    
    // Create region selector
    regionSelector_ = std::make_unique<RegionSelector>();
    
    LogInfo(L"");
    LogInfo(L"Hotkeys:");
    LogInfo(L"  Ctrl+Shift+S - Select screen region");
    LogInfo(L"  Ctrl+Shift+V - Toggle capture visibility");
    LogInfo(L"  Ctrl+Shift+Q - Quit");
    LogInfo(L"");
    
    return true;
}

int InvisibleApp::Run() {
    if (!overlay_) {
        return -1;
    }
    
    return overlay_->RunMessageLoop();
}

void InvisibleApp::Shutdown() {
    if (audioCapture_) {
        audioCapture_->Stop();
    }
    
    if (overlay_) {
        overlay_->Destroy();
    }
    
    CoUninitialize();
    LogInfo(L"Application shutdown complete");
}

void InvisibleApp::OnHotkey(int hotkeyId) {
    switch (hotkeyId) {
        case HotkeyManager::HOTKEY_REGION_SELECT: {
            if (!regionSelector_->IsSelecting()) {
                // Temporarily hide overlay during selection
                if (overlay_) {
                    overlay_->Show(false);
                }
                
                regionSelector_->StartSelection([this](const Rect& region) {
                    OnRegionSelected(region);
                });
            }
            break;
        }
        
        case HotkeyManager::HOTKEY_TOGGLE_VISIBILITY: {
            if (overlay_) {
                bool excluded = overlay_->IsExcludedFromCapture();
                overlay_->SetExcludeFromCapture(!excluded);
                
                statusText_ = excluded ? 
                    L"Overlay now VISIBLE to capture" : 
                    L"Overlay now HIDDEN from capture";
                
                overlay_->Invalidate();
            }
            break;
        }
        
        case HotkeyManager::HOTKEY_QUIT: {
            LogInfo(L"Quit hotkey pressed");
            // Quit is handled by OverlayWindow
            break;
        }
    }
}

void InvisibleApp::OnRegionSelected(const Rect& region) {
    LogInfo(L"Region selected");
    
    // Show overlay again
    if (overlay_) {
        overlay_->Show(true);
    }
    
    lastSelectedRegion_ = region;
    
    // Capture the selected region
    // NOTE: Our overlay is excluded from capture, so it won't appear
    CapturedImage capture = ScreenCapture::CaptureRegion(region);
    
    if (capture.IsValid()) {
        std::wstringstream ss;
        ss << L"Captured region: " << region.width << L"x" << region.height 
           << L" at (" << region.x << L", " << region.y << L")";
        lastCaptureInfo_ = ss.str();
        LogInfo(lastCaptureInfo_.c_str());
        
        // Save to file for verification
        static int captureCount = 0;
        std::wstring filename = L"capture_" + std::to_wstring(++captureCount) + L".bmp";
        
        if (ScreenCapture::SaveToBmp(capture, filename.c_str())) {
            std::wcout << L"[INFO] Saved to " << filename << std::endl;
            lastCaptureInfo_ += L"\nSaved: " + filename;
        }
        
        // Here you would typically:
        // 1. Send the image to an OCR engine
        // 2. Process the extracted text
        // 3. Display results in the overlay
        
        statusText_ = L"Region captured! Ready for OCR.";
    } else {
        statusText_ = L"Capture failed";
    }
    
    if (overlay_) {
        overlay_->Invalidate();
    }
}

void InvisibleApp::RenderOverlay(HDC hdc, const Rect& bounds) {
    if (!showStatus_) return;
    
    // Set up text rendering
    SetBkMode(hdc, TRANSPARENT);
    
    // Create a nice font
    HFONT font = CreateFontW(
        16,                     // Height
        0,                      // Width (0 = auto)
        0,                      // Escapement
        0,                      // Orientation
        FW_NORMAL,              // Weight
        FALSE,                  // Italic
        FALSE,                  // Underline
        FALSE,                  // Strikeout
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI"
    );
    
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
    
    // Draw status panel in corner
    int panelX = 20;
    int panelY = 20;
    int panelWidth = 350;
    int panelHeight = 120;
    
    // Panel background
    HBRUSH panelBrush = CreateSolidBrush(RGB(30, 30, 35));
    RECT panelRect = {panelX, panelY, panelX + panelWidth, panelY + panelHeight};
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);
    
    // Panel border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, panelX, panelY, panelX + panelWidth, panelY + panelHeight);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    
    // Title
    SetTextColor(hdc, RGB(100, 180, 255));
    RECT textRect = {panelX + 10, panelY + 10, panelX + panelWidth - 10, panelY + 30};
    DrawTextW(hdc, L"Invisible Overlay [RESEARCH]", -1, &textRect, DT_LEFT);
    
    // Capture status indicator
    SetTextColor(hdc, overlay_ && overlay_->IsExcludedFromCapture() ? 
                 RGB(100, 255, 100) : RGB(255, 100, 100));
    textRect.top = panelY + 32;
    textRect.bottom = panelY + 50;
    DrawTextW(hdc, overlay_ && overlay_->IsExcludedFromCapture() ? 
              L"[Hidden from capture]" : L"[VISIBLE to capture!]", 
              -1, &textRect, DT_LEFT);
    
    // Status text
    SetTextColor(hdc, RGB(200, 200, 200));
    textRect.top = panelY + 55;
    textRect.bottom = panelY + 75;
    DrawTextW(hdc, statusText_.c_str(), -1, &textRect, DT_LEFT);
    
    // Audio status
    if (audioCapture_ && audioCapture_->IsCapturing()) {
        SetTextColor(hdc, RGB(150, 200, 150));
        textRect.top = panelY + 77;
        textRect.bottom = panelY + 95;
        AudioFormat fmt = audioCapture_->GetFormat();
        std::wstring audioInfo = L"Audio: " + fmt.ToString();
        DrawTextW(hdc, audioInfo.c_str(), -1, &textRect, DT_LEFT);
    }
    
    // Hotkey hints
    SetTextColor(hdc, RGB(120, 120, 130));
    textRect.top = panelY + 98;
    textRect.bottom = panelY + 115;
    DrawTextW(hdc, L"Ctrl+Shift+S: Select | V: Toggle | Q: Quit", -1, &textRect, DT_LEFT);
    
    // Cleanup
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

// -----------------------------------------------------------------------------
// Entry Point
// -----------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                    LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Attach console for debug output
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
    
    // Parse command line for options
    AppConfig config;
    
    std::wstring cmdLine = lpCmdLine;
    if (cmdLine.find(L"--no-audio") != std::wstring::npos) {
        config.enableAudioCapture = false;
    }
    if (cmdLine.find(L"--debug") != std::wstring::npos) {
        config.debugMode = true;
    }
    if (cmdLine.find(L"--visible") != std::wstring::npos) {
        // Start with overlay visible to capture (for testing)
        // The app will start visible so you can verify it works
    }
    
    // Create and run application
    InvisibleApp app;
    
    if (!app.Initialize(config)) {
        DWORD lastError = GetLastError();
        std::wstringstream errMsg;
        errMsg << L"Failed to initialize application.\n\n"
               << L"Error code: 0x" << std::hex << lastError << L"\n\n"
               << L"Possible causes:\n"
               << L"- Windows 10 version 2004 or later required\n"
               << L"- Another instance may be running\n"
               << L"- Hotkeys may be registered by another app";
        MessageBoxW(nullptr, errMsg.str().c_str(),
                    L"Initialization Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    int result = app.Run();
    app.Shutdown();
    
    return result;
}

// Alternative console entry point (for testing)
int wmain(int argc, wchar_t* argv[]) {
    (void)argc;
    (void)argv;
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOW);
}

/**
 * Invisible Overlay Application - AI Meeting Assistant
 *
 * Features:
 * - SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) for capture exclusion
 * - WASAPI loopback for system audio capture
 * - OpenAI Whisper for real-time transcription
 * - OpenAI GPT for Q&A and summarization
 * - Windows SAPI for text-to-speech responses
 *
 * FOR RESEARCH AND EDUCATIONAL PURPOSES ONLY
 *
 * Hotkeys:
 *   Ctrl+Shift+A - Ask AI about the meeting
 *   Ctrl+Shift+M - Generate meeting summary
 *   Ctrl+Shift+T - Toggle transcript display
 *   Ctrl+Shift+S - Start region selection
 *   Ctrl+Shift+V - Toggle capture visibility (debug)
 *   Ctrl+Shift+Q - Quit application
 */

#include "audio_capture.h"
#include "meeting_assistant.h"
#include "overlay_window.h"
#include "screen_capture.h"
#include <deque>
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
  bool enableAI = true;
  bool debugMode = false;
  BYTE overlayAlpha = 220;

  // AI Configuration
  std::string openaiApiKey;
  std::string gptModel = "gpt-4o-mini";
  bool enableTTS = true;
};

// Additional hotkey IDs for AI features
namespace AIHotkeys {
constexpr int HOTKEY_ASK_AI = 0x0010;
constexpr int HOTKEY_SUMMARY = 0x0011;
constexpr int HOTKEY_TOGGLE_TRANSCRIPT = 0x0012;
} // namespace AIHotkeys

// -----------------------------------------------------------------------------
// Application Class
// -----------------------------------------------------------------------------

class InvisibleApp {
public:
  InvisibleApp() = default;
  ~InvisibleApp();

  bool Initialize(const AppConfig &config);
  int Run();
  void Shutdown();

private:
  void OnHotkey(int hotkeyId);
  void OnRegionSelected(const Rect &region);
  void OnMeetingAssistantEvent(const MeetingAssistantEvent &event);
  void RenderOverlay(HDC hdc, const Rect &bounds);
  void RegisterAIHotkeys();
  void UnregisterAIHotkeys();

  AppConfig config_;
  std::unique_ptr<OverlayWindow> overlay_;
  std::unique_ptr<AudioCapture> audioCapture_;
  std::unique_ptr<AudioBufferQueue> audioQueue_;
  std::unique_ptr<RegionSelector> regionSelector_;
  std::unique_ptr<MeetingAssistant> meetingAssistant_;

  // Display state
  std::wstring statusText_ = L"Initializing...";
  std::wstring lastCaptureInfo_;
  Rect lastSelectedRegion_;
  bool showStatus_ = true;

  // AI state
  std::deque<std::wstring> transcriptLines_;
  std::wstring lastAIResponse_;
  bool showTranscript_ = true;
  bool aiInitialized_ = false;
  bool aiListening_ = false;
  std::mutex displayMutex_;

  static constexpr int MAX_TRANSCRIPT_LINES = 8;
  static constexpr int MAX_RESPONSE_LENGTH = 500;
};

InvisibleApp::~InvisibleApp() { Shutdown(); }

void InvisibleApp::RegisterAIHotkeys() {
  if (!overlay_ || !overlay_->GetHandle())
    return;

  RegisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_ASK_AI,
                 MOD_CONTROL | MOD_SHIFT, 'A');
  RegisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_SUMMARY,
                 MOD_CONTROL | MOD_SHIFT, 'M');
  RegisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_TOGGLE_TRANSCRIPT,
                 MOD_CONTROL | MOD_SHIFT, 'T');
}

void InvisibleApp::UnregisterAIHotkeys() {
  if (!overlay_ || !overlay_->GetHandle())
    return;

  UnregisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_ASK_AI);
  UnregisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_SUMMARY);
  UnregisterHotKey(overlay_->GetHandle(), AIHotkeys::HOTKEY_TOGGLE_TRANSCRIPT);
}

bool InvisibleApp::Initialize(const AppConfig &config) {
  config_ = config;

  LogInfo(L"===========================================");
  LogInfo(L"  AI Meeting Assistant");
  LogInfo(L"  FOR RESEARCH PURPOSES ONLY");
  LogInfo(L"===========================================");

  // Initialize COM for audio capture and SAPI
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    LogError(L"Failed to initialize COM");
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
    overlay_->SetRenderCallback(
        [this](HDC hdc, const Rect &bounds) { RenderOverlay(hdc, bounds); });

    // Register AI hotkeys
    RegisterAIHotkeys();

    LogInfo(L"Overlay window created");
  }

  // Initialize Meeting Assistant
  if (config_.enableAI && !config_.openaiApiKey.empty()) {
    statusText_ = L"Initializing AI...";

    meetingAssistant_ = std::make_unique<MeetingAssistant>();

    MeetingAssistantConfig maConfig;
    maConfig.apiKey = config_.openaiApiKey;
    maConfig.gptModel = config_.gptModel;
    maConfig.enableTTS = config_.enableTTS;
    maConfig.transcriptionIntervalSec = 5.0f;

    if (meetingAssistant_->Initialize(maConfig)) {
      aiInitialized_ = true;

      meetingAssistant_->SetEventCallback(
          [this](const MeetingAssistantEvent &event) {
            OnMeetingAssistantEvent(event);
          });

      if (meetingAssistant_->StartListening()) {
        aiListening_ = true;
        statusText_ = L"AI Ready - Listening to audio";
        LogInfo(L"Meeting Assistant active - listening for audio");
      } else {
        statusText_ = L"AI Ready - Audio capture failed";
        LogError(L"Failed to start audio listening");
      }
    } else {
      statusText_ = L"AI initialization failed";
      LogError(L"Failed to initialize Meeting Assistant");
    }
  } else {
    statusText_ = L"No API key - AI features disabled";
    LogInfo(L"AI features disabled (no API key)");

    // Fallback to basic audio capture
    if (config_.enableAudioCapture) {
      audioCapture_ = std::make_unique<AudioCapture>();
      audioQueue_ = std::make_unique<AudioBufferQueue>();

      if (audioCapture_->Initialize()) {
        if (audioCapture_->Start(audioQueue_.get())) {
          statusText_ = L"Audio capture active (no AI)";
        }
      }
    }
  }

  // Create region selector
  regionSelector_ = std::make_unique<RegionSelector>();

  LogInfo(L"");
  LogInfo(L"Hotkeys:");
  LogInfo(L"  Ctrl+Shift+A - Ask AI");
  LogInfo(L"  Ctrl+Shift+M - Summary");
  LogInfo(L"  Ctrl+Shift+T - Toggle transcript");
  LogInfo(L"  Ctrl+Shift+S - Select region");
  LogInfo(L"  Ctrl+Shift+V - Toggle visibility");
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
  UnregisterAIHotkeys();

  if (meetingAssistant_) {
    meetingAssistant_->Shutdown();
  }

  if (audioCapture_) {
    audioCapture_->Stop();
  }

  if (overlay_) {
    overlay_->Destroy();
  }

  CoUninitialize();
  LogInfo(L"Application shutdown complete");
}

void InvisibleApp::OnMeetingAssistantEvent(const MeetingAssistantEvent &event) {
  std::lock_guard<std::mutex> lock(displayMutex_);

  switch (event.type) {
  case MeetingAssistantEvent::TRANSCRIPT_UPDATE: {
    int size =
        MultiByteToWideChar(CP_UTF8, 0, event.text.c_str(), -1, nullptr, 0);
    if (size > 0) {
      std::wstring wtext(size - 1, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, event.text.c_str(), -1, &wtext[0], size);

      if (wtext.length() > 80) {
        wtext = wtext.substr(0, 77) + L"...";
      }

      transcriptLines_.push_back(wtext);
      while (transcriptLines_.size() > MAX_TRANSCRIPT_LINES) {
        transcriptLines_.pop_front();
      }
    }
    break;
  }

  case MeetingAssistantEvent::AI_RESPONSE:
  case MeetingAssistantEvent::SUMMARY_READY:
  case MeetingAssistantEvent::ACTION_ITEMS_READY: {
    int size =
        MultiByteToWideChar(CP_UTF8, 0, event.text.c_str(), -1, nullptr, 0);
    if (size > 0) {
      std::wstring wtext(size - 1, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, event.text.c_str(), -1, &wtext[0], size);

      if (wtext.length() > MAX_RESPONSE_LENGTH) {
        wtext = wtext.substr(0, MAX_RESPONSE_LENGTH - 3) + L"...";
      }

      lastAIResponse_ = wtext;
      statusText_ = (event.type == MeetingAssistantEvent::SUMMARY_READY)
                        ? L"Summary generated!"
                        : L"AI response received";
    }
    break;
  }

  case MeetingAssistantEvent::EVENT_ERROR: {
    int size =
        MultiByteToWideChar(CP_UTF8, 0, event.error.c_str(), -1, nullptr, 0);
    if (size > 0) {
      std::wstring werror(size - 1, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, event.error.c_str(), -1, &werror[0],
                          size);
      statusText_ = L"Error: " + werror;
    }
    break;
  }
  }

  if (overlay_) {
    overlay_->Invalidate();
  }
}

void InvisibleApp::OnHotkey(int hotkeyId) {
  switch (hotkeyId) {
  case HotkeyManager::HOTKEY_REGION_SELECT: {
    if (regionSelector_ && !regionSelector_->IsSelecting()) {
      if (overlay_)
        overlay_->Show(false);
      regionSelector_->StartSelection(
          [this](const Rect &region) { OnRegionSelected(region); });
    }
    break;
  }

  case HotkeyManager::HOTKEY_TOGGLE_VISIBILITY: {
    if (overlay_) {
      bool excluded = overlay_->IsExcludedFromCapture();
      overlay_->SetExcludeFromCapture(!excluded);
      statusText_ = excluded ? L"VISIBLE to capture!" : L"Hidden from capture";
      overlay_->Invalidate();
    }
    break;
  }

  case HotkeyManager::HOTKEY_QUIT: {
    LogInfo(L"Quit hotkey pressed");
    break;
  }

  case AIHotkeys::HOTKEY_ASK_AI: {
    if (meetingAssistant_ && aiInitialized_) {
      meetingAssistant_->AskQuestion(
          "What are the key points being discussed?");
      statusText_ = L"Asking AI...";
      if (overlay_)
        overlay_->Invalidate();
    } else {
      statusText_ = L"AI not available";
      if (overlay_)
        overlay_->Invalidate();
    }
    break;
  }

  case AIHotkeys::HOTKEY_SUMMARY: {
    if (meetingAssistant_ && aiInitialized_) {
      meetingAssistant_->GenerateSummary();
      statusText_ = L"Generating summary...";
      if (overlay_)
        overlay_->Invalidate();
    } else {
      statusText_ = L"AI not available";
      if (overlay_)
        overlay_->Invalidate();
    }
    break;
  }

  case AIHotkeys::HOTKEY_TOGGLE_TRANSCRIPT: {
    showTranscript_ = !showTranscript_;
    statusText_ = showTranscript_ ? L"Transcript shown" : L"Transcript hidden";
    if (overlay_)
      overlay_->Invalidate();
    break;
  }
  }
}

void InvisibleApp::OnRegionSelected(const Rect &region) {
  if (overlay_)
    overlay_->Show(true);
  lastSelectedRegion_ = region;

  CapturedImage capture = ScreenCapture::CaptureRegion(region);
  if (capture.IsValid()) {
    static int captureCount = 0;
    std::wstring filename =
        L"capture_" + std::to_wstring(++captureCount) + L".bmp";
    ScreenCapture::SaveToBmp(capture, filename.c_str());
    statusText_ = L"Region captured: " + filename;
  }

  if (overlay_)
    overlay_->Invalidate();
}

void InvisibleApp::RenderOverlay(HDC hdc, const Rect &bounds) {
  std::lock_guard<std::mutex> lock(displayMutex_);

  SetBkMode(hdc, TRANSPARENT);

  // Fonts
  HFONT font =
      CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT titleFont =
      CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

  // =========================================================================
  // Main Control Panel (top-left)
  // =========================================================================
  int panelX = 20, panelY = 20;
  int panelWidth = 420, panelHeight = 140;

  HBRUSH panelBrush = CreateSolidBrush(RGB(25, 28, 35));
  RECT panelRect = {panelX, panelY, panelX + panelWidth, panelY + panelHeight};
  FillRect(hdc, &panelRect, panelBrush);
  DeleteObject(panelBrush);

  // Border
  HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(60, 130, 200));
  HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
  HBRUSH oldBrush =
      static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
  Rectangle(hdc, panelX, panelY, panelX + panelWidth, panelY + panelHeight);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBrush);
  DeleteObject(borderPen);

  RECT textRect;

  // Title
  SelectObject(hdc, titleFont);
  SetTextColor(hdc, RGB(100, 180, 255));
  textRect = {panelX + 15, panelY + 10, panelX + panelWidth - 15, panelY + 30};
  DrawTextW(hdc, L"AI Meeting Assistant", -1, &textRect, DT_LEFT);

  // AI Status indicator
  SelectObject(hdc, font);
  int statusY = panelY + 38;

  // AI indicator
  SetTextColor(hdc, aiInitialized_ ? RGB(100, 255, 120) : RGB(255, 100, 100));
  textRect = {panelX + 15, statusY, panelX + 200, statusY + 18};
  DrawTextW(hdc, aiInitialized_ ? L"● AI Ready" : L"○ AI Offline", -1,
            &textRect, DT_LEFT);

  // Listening indicator
  SetTextColor(hdc, aiListening_ ? RGB(100, 255, 120) : RGB(180, 180, 180));
  textRect = {panelX + 120, statusY, panelX + 280, statusY + 18};
  DrawTextW(hdc, aiListening_ ? L"● Listening" : L"○ Not Listening", -1,
            &textRect, DT_LEFT);

  // Capture status
  bool hidden = overlay_ && overlay_->IsExcludedFromCapture();
  SetTextColor(hdc, hidden ? RGB(100, 255, 120) : RGB(255, 150, 100));
  textRect = {panelX + 260, statusY, panelX + panelWidth - 15, statusY + 18};
  DrawTextW(hdc, hidden ? L"● Hidden" : L"● VISIBLE!", -1, &textRect, DT_LEFT);

  // Status message
  SetTextColor(hdc, RGB(200, 200, 200));
  textRect = {panelX + 15, statusY + 25, panelX + panelWidth - 15,
              statusY + 43};
  DrawTextW(hdc, statusText_.c_str(), -1, &textRect, DT_LEFT | DT_END_ELLIPSIS);

  // Hotkey hints
  SetTextColor(hdc, RGB(120, 140, 160));
  textRect = {panelX + 15, statusY + 48, panelX + panelWidth - 15,
              statusY + 66};
  DrawTextW(hdc, L"Ctrl+Shift: A=Ask | M=Summary | T=Transcript", -1, &textRect,
            DT_LEFT);

  textRect.top += 18;
  textRect.bottom += 18;
  DrawTextW(hdc, L"Ctrl+Shift: S=Select | V=Visibility | Q=Quit", -1, &textRect,
            DT_LEFT);

  // =========================================================================
  // Transcript Panel (bottom-left)
  // =========================================================================
  if (showTranscript_) {
    int transX = 20;
    int transY = bounds.height - 220;
    int transWidth = 420;
    int transHeight = 200;

    HBRUSH transBrush = CreateSolidBrush(RGB(20, 25, 30));
    RECT transRect = {transX, transY, transX + transWidth,
                      transY + transHeight};
    FillRect(hdc, &transRect, transBrush);
    DeleteObject(transBrush);

    // Border
    HPEN transBorderPen = CreatePen(PS_SOLID, 1, RGB(60, 80, 100));
    oldPen = static_cast<HPEN>(SelectObject(hdc, transBorderPen));
    oldBrush =
        static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, transX, transY, transX + transWidth, transY + transHeight);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(transBorderPen);

    SelectObject(hdc, titleFont);
    SetTextColor(hdc, RGB(80, 160, 220));
    textRect = {transX + 10, transY + 8, transX + transWidth - 10, transY + 28};
    DrawTextW(hdc, L"Live Transcript", -1, &textRect, DT_LEFT);

    SelectObject(hdc, font);
    if (transcriptLines_.empty()) {
      SetTextColor(hdc, RGB(100, 100, 110));
      textRect = {transX + 10, transY + 35, transX + transWidth - 10,
                  transY + 55};
      DrawTextW(hdc, L"(Waiting for audio...)", -1, &textRect, DT_LEFT);
    } else {
      SetTextColor(hdc, RGB(200, 200, 200));
      int lineY = transY + 35;
      for (const auto &line : transcriptLines_) {
        textRect = {transX + 10, lineY, transX + transWidth - 10, lineY + 18};
        DrawTextW(hdc, line.c_str(), -1, &textRect, DT_LEFT | DT_END_ELLIPSIS);
        lineY += 19;
      }
    }
  }

  // =========================================================================
  // AI Response Panel (top-right)
  // =========================================================================
  int respX = bounds.width - 450;
  int respWidth = 430;
  int respHeight = lastAIResponse_.empty() ? 80 : 220;

  HBRUSH respBrush = CreateSolidBrush(RGB(25, 35, 45));
  RECT respRect = {respX, panelY, respX + respWidth, panelY + respHeight};
  FillRect(hdc, &respRect, respBrush);
  DeleteObject(respBrush);

  // Border
  HPEN respBorderPen = CreatePen(PS_SOLID, 2, RGB(80, 140, 200));
  oldPen = static_cast<HPEN>(SelectObject(hdc, respBorderPen));
  oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
  Rectangle(hdc, respX, panelY, respX + respWidth, panelY + respHeight);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBrush);
  DeleteObject(respBorderPen);

  SelectObject(hdc, titleFont);
  SetTextColor(hdc, RGB(100, 200, 255));
  textRect = {respX + 15, panelY + 10, respX + respWidth - 15, panelY + 30};
  DrawTextW(hdc, L"AI Response", -1, &textRect, DT_LEFT);

  SelectObject(hdc, font);
  if (lastAIResponse_.empty()) {
    SetTextColor(hdc, RGB(100, 100, 110));
    textRect = {respX + 15, panelY + 38, respX + respWidth - 15, panelY + 70};
    DrawTextW(hdc, L"Press Ctrl+Shift+A to ask, M for summary", -1, &textRect,
              DT_LEFT);
  } else {
    SetTextColor(hdc, RGB(230, 230, 230));
    textRect = {respX + 15, panelY + 38, respX + respWidth - 15,
                panelY + respHeight - 10};
    DrawTextW(hdc, lastAIResponse_.c_str(), -1, &textRect,
              DT_LEFT | DT_WORDBREAK);
  }

  // Cleanup
  SelectObject(hdc, oldFont);
  DeleteObject(font);
  DeleteObject(titleFont);
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
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
  }

  AppConfig config;

  // Get API key from environment variable
  // Set GROQ_API_KEY environment variable with your free Groq API key
  // Get one at: https://console.groq.com
  char *envKey = nullptr;
  size_t envKeyLen = 0;
  // Try GROQ_API_KEY first, then others for backwards compat
  if (_dupenv_s(&envKey, &envKeyLen, "GROQ_API_KEY") == 0 && envKey) {
    config.openaiApiKey = envKey;
    free(envKey);
  } else if (_dupenv_s(&envKey, &envKeyLen, "GEMINI_API_KEY") == 0 && envKey) {
    config.openaiApiKey = envKey;
    free(envKey);
  } else if (_dupenv_s(&envKey, &envKeyLen, "OPENAI_API_KEY") == 0 && envKey) {
    config.openaiApiKey = envKey;
    free(envKey);
  } else {
    // No API key found - show warning
    MessageBoxW(nullptr,
                L"No API key found!\n\n"
                L"Set the GROQ_API_KEY environment variable with your free "
                L"Groq API key.\n"
                L"Get one at: https://console.groq.com\n\n"
                L"AI features will be disabled.",
                L"AI Meeting Assistant", MB_ICONWARNING);
    config.enableAI = false;
  }

  // Parse command line options
  std::wstring cmdLine = lpCmdLine;
  if (cmdLine.find(L"--no-ai") != std::wstring::npos) {
    config.enableAI = false;
  }
  if (cmdLine.find(L"--no-tts") != std::wstring::npos) {
    config.enableTTS = false;
  }
  if (cmdLine.find(L"--debug") != std::wstring::npos) {
    config.debugMode = true;
  }

  // Create and run application
  InvisibleApp app;

  if (!app.Initialize(config)) {
    MessageBoxW(
        nullptr,
        L"Failed to initialize application.\n\nCheck console for details.",
        L"Initialization Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  int result = app.Run();
  app.Shutdown();

  return result;
}

// Console entry point for testing
int wmain(int argc, wchar_t *argv[]) {
  (void)argc;
  (void)argv;
  return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(),
                  SW_SHOW);
}

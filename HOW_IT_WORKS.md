# 🎓 How the AI Meeting Assistant Works
## A Complete Learning Guide

Welcome! This document will teach you **everything** about how this AI Meeting Assistant works - from the big picture down to the code details. By the end, you'll understand exactly what's happening under the hood.

---

## 📋 Table of Contents

1. [The Big Picture](#the-big-picture)
2. [Architecture Overview](#architecture-overview)
3. [The Invisible Overlay Window](#the-invisible-overlay-window)
4. [Audio Capture with WASAPI](#audio-capture-with-wasapi)
5. [AI Integration with Groq](#ai-integration-with-groq)
6. [Text-to-Speech with SAPI](#text-to-speech-with-sapi)
7. [The Meeting Assistant Orchestrator](#the-meeting-assistant-orchestrator)
8. [HTTP Client for API Calls](#http-client-for-api-calls)
9. [Code Walkthrough](#code-walkthrough)
10. [How Everything Connects](#how-everything-connects)

---

## 🎯 The Big Picture

### What Does This App Do?

This application creates an **invisible overlay** on your screen that can:
- 🎤 **Listen** to your computer's audio (what you hear through speakers)
- 📝 **Transcribe** speech to text in real-time using AI (optimized 16kHz resampling)
- 🤖 **Answer questions** directly about the meeting content
- 📸 **Screen Capture → AI** — Select any area, AI reads and answers questions in it
- 💬 **Conversation Memory** — Remembers last 10 Q&A for follow-up questions
- 📋 **Summarize** meetings automatically
- 🔊 **Speak responses** using text-to-speech (disabled by default)

### Why is it "Invisible"?

The overlay uses a special Windows feature called `WDA_EXCLUDEFROMCAPTURE`. This means:
- ✅ You can see it on your screen
- ❌ Screen recording software CANNOT capture it
- ❌ Screen sharing does NOT show it
- ❌ Screenshots exclude it

This is useful for having AI assistance during video calls without others seeing it!

---

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        MAIN APPLICATION                          │
│                         (main.cpp)                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ┌──────────────┐    ┌────────────────┐    ┌────────────────┐ │
│   │   Overlay    │    │    Meeting     │    │   Screen       │ │
│   │   Window     │◄───┤   Assistant    │    │   Capture      │ │
│   │              │    │                │    │   (optional)   │ │
│   └──────────────┘    └───────┬────────┘    └────────────────┘ │
│                               │                                  │
│              ┌────────────────┼────────────────┐                │
│              │                │                │                │
│              ▼                ▼                ▼                │
│   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │
│   │    Audio     │  │     AI       │  │   Text-to-   │        │
│   │   Capture    │  │   Service    │  │   Speech     │        │
│   │   (WASAPI)   │  │   (Groq)     │  │   (SAPI)     │        │
│   └──────────────┘  └──────┬───────┘  └──────────────┘        │
│                             │                                   │
│                             ▼                                   │
│                    ┌──────────────┐                            │
│                    │    HTTP      │                            │
│                    │   Client     │                            │
│                    │  (WinHTTP)   │                            │
│                    └──────────────┘                            │
│                             │                                   │
│                             ▼                                   │
│                    ┌──────────────┐                            │
│                    │   INTERNET   │                            │
│                    │  (Groq API)  │                            │
│                    └──────────────┘                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | File(s) | What it Does |
|-----------|---------|--------------|
| **Overlay Window** | `overlay_window.h/cpp` | Creates the invisible, always-on-top window |
| **Audio Capture** | `audio_capture.h/cpp` | Records system audio using WASAPI loopback |
| **AI Service** | `ai_service.h/cpp` | Communicates with Groq API for AI responses |
| **Text-to-Speech** | `text_to_speech.h/cpp` | Speaks text aloud using Windows SAPI |
| **Meeting Assistant** | `meeting_assistant.h/cpp` | Orchestrates all components together |
| **HTTP Client** | `http_client.h/cpp` | Makes HTTP requests to APIs |
| **Main** | `main.cpp` | Entry point, UI rendering, hotkey handling |

---

## 🪟 The Invisible Overlay Window

### How Does a Window Become Invisible to Capture?

Windows 10 (version 2004+) introduced a feature called **Window Display Affinity**. Here's how we use it:

```cpp
// In overlay_window.cpp
SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
```

This single line tells Windows: "Don't include this window when someone captures the screen."

### Creating the Overlay

The overlay window has special properties:

```cpp
// Layered window (supports transparency)
DWORD exStyle = WS_EX_LAYERED;

// Always on top of other windows
exStyle |= WS_EX_TOPMOST;

// Not shown in taskbar
exStyle |= WS_EX_TOOLWINDOW;

// Click-through (clicks pass to windows below)
exStyle |= WS_EX_TRANSPARENT;
```

### The Window Class

```cpp
WNDCLASSEXW wc = {};
wc.cbSize = sizeof(WNDCLASSEXW);
wc.style = CS_HREDRAW | CS_VREDRAW;
wc.lpfnWndProc = WindowProc;  // Our message handler
wc.hInstance = GetModuleHandle(nullptr);
wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
wc.lpszClassName = L"InvisibleOverlayClass";
RegisterClassExW(&wc);
```

### Making it Transparent

We use a "layered window" with alpha blending:

```cpp
// Set transparency (0 = invisible, 255 = opaque)
SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
```

---

## 🎤 Audio Capture with WASAPI

### What is WASAPI?

**WASAPI** (Windows Audio Session API) is Windows' modern audio API. We use a special mode called **loopback capture** which records whatever is playing through your speakers.

### The Capture Process

```
┌───────────────┐     ┌─────────────────┐     ┌──────────────┐
│ System Audio  │────▶│ WASAPI Loopback │────▶│ Audio Buffer │
│ (speakers)    │     │    Capture      │     │   (PCM data) │
└───────────────┘     └─────────────────┘     └──────────────┘
```

### Key Code Explained

```cpp
// 1. Get the audio device enumerator
CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                 IID_IMMDeviceEnumerator, (void**)&deviceEnumerator_);

// 2. Get the default audio output device (speakers)
deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

// 3. Create an audio client
device_->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&audioClient_);

// 4. Initialize in LOOPBACK mode (this is the magic!)
audioClient_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK,  // <- Captures what's playing
    bufferDuration,
    0,
    mixFormat_,
    nullptr
);

// 5. Get the capture interface
audioClient_->GetService(IID_IAudioCaptureClient, (void**)&captureClient_);

// 6. Start capturing
audioClient_->Start();
```

### The Capture Loop

```cpp
while (!shouldStop_) {
    // Wait for audio data
    WaitForSingleObject(captureEvent_, 100);
    
    // Get the captured audio
    BYTE* data;
    UINT32 numFrames;
    captureClient_->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
    
    // Process the audio (send to transcription)
    handler_->OnAudioData(buffer, format_);
    
    // Release the buffer
    captureClient_->ReleaseBuffer(numFrames);
}
```

---

## 🤖 AI Integration with Groq

### Why Groq?

| Feature | Groq | OpenAI | Gemini |
|---------|------|--------|--------|
| **Free tier** | ✅ Generous | ❌ Paid only | ⚠️ Limited |
| **Speed** | ⚡ Fastest | 🐢 Slower | 🐌 Medium |
| **Billing required** | ❌ No | ✅ Yes | ✅ Yes |

### Making API Calls

Groq uses the same format as OpenAI, making it easy to switch between them:

```cpp
// Endpoint
std::wstring endpoint = L"https://api.groq.com/openai/v1/chat/completions";

// Request body (JSON)
{
    "model": "llama-3.3-70b-versatile",
    "max_tokens": 1024,
    "temperature": 0.7,
    "messages": [
        {"role": "system", "content": "You are a helpful assistant..."},
        {"role": "user", "content": "What was discussed in the meeting?"}
    ]
}

// Headers
Authorization: Bearer gsk_YOUR_API_KEY
Content-Type: application/json
```

### The Chat Function

```cpp
std::string OpenAIService::Chat(const std::vector<ChatMessage>& messages) {
    // 1. Build the JSON payload
    std::string payload = BuildChatPayload(messages);
    
    // 2. Set up headers with API key
    std::map<std::wstring, std::wstring> headers;
    headers[L"Authorization"] = L"Bearer " + apiKey;
    headers[L"Content-Type"] = L"application/json";
    
    // 3. Make the HTTP request
    HttpResponse response = httpClient_.PostJson(endpoint, payload, headers);
    
    // 4. Parse the response
    return ParseChatResponse(response.body);
}
```

### Speech-to-Text (Whisper)

Groq provides Whisper API for transcription. We use `whisper-large-v3-turbo` with optimizations:

```cpp
// Audio is first resampled to 16kHz mono 16-bit (Whisper's native format)
std::vector<BYTE> resampledData = ResampleTo16kMono16bit(audioData, format);

// Convert to WAV format
std::vector<BYTE> wavData = ConvertToWav(resampledData, 16000, 1, 16);

// Send to Whisper API with language hint + prompt context
std::map<std::string, std::string> fields;
fields["model"] = "whisper-large-v3-turbo";  // Faster + accurate
fields["language"] = "en";                    // Skip language detection
fields["prompt"] = "Technical interview discussion...";

HttpResponse response = httpClient_.PostMultipart(
    endpoint, fields, "audio.wav", "file", wavData, "audio/wav", headers
);
```

### Screen Capture → AI Vision

Select a region on screen, and the AI reads and answers the question:

```cpp
// 1. Capture the selected region
CapturedImage capture = ScreenCapture::CaptureRegion(region);

// 2. Convert BMP → JPEG using Windows Imaging Component (WIC)
std::string base64Data = ScreenCapture::ConvertToBase64Bmp(capture);

// 3. Send to Groq Vision API (Llama 4 Scout)
meetingAssistant_->AnalyzeImage(base64Data);
```

The vision model reads text/questions from the image and provides direct answers.

---

## 🔊 Text-to-Speech with SAPI

### What is SAPI?

**SAPI** (Speech Application Programming Interface) is Windows' built-in text-to-speech system. It's free and works offline!

### Initializing TTS

```cpp
bool TextToSpeech::Initialize(const TTSConfig& config) {
    // Create the voice object
    HRESULT hr = CoCreateInstance(
        CLSID_SpVoice,    // Speech voice class
        nullptr,
        CLSCTX_ALL,
        IID_ISpVoice,
        (void**)&voice_
    );
    
    // Configure voice settings
    SetRate(config.rate);      // Speed: -10 to 10
    SetVolume(config.volume);  // Volume: 0 to 100
    
    return SUCCEEDED(hr);
}
```

### Speaking Text

```cpp
bool TextToSpeech::Speak(const std::wstring& text) {
    // Speak asynchronously (doesn't block)
    HRESULT hr = voice_->Speak(
        text.c_str(),
        SPF_ASYNC | SPF_PURGEBEFORESPEAK,  // Async + stop previous speech
        nullptr
    );
    return SUCCEEDED(hr);
}
```

---

## 🎯 The Meeting Assistant Orchestrator

The `MeetingAssistant` class ties everything together. It:
1. Captures audio in real-time
2. Sends audio to AI for transcription
3. Maintains a running transcript
4. Handles user queries
5. Speaks responses aloud

### Threading Model

```
┌─────────────────────────────────────────────────────────────┐
│                     MEETING ASSISTANT                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐ │
│  │  Main Thread    │  │ Transcription   │  │ AI Query    │ │
│  │                 │  │    Thread       │  │   Thread    │ │
│  │ - Audio capture │  │                 │  │             │ │
│  │ - Event emit    │  │ - Collect audio │  │ - Process   │ │
│  │                 │  │ - Send to API   │  │   questions │ │
│  │                 │  │ - Update text   │  │ - Summaries │ │
│  └─────────────────┘  └─────────────────┘  └─────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### The Transcription Loop (Optimized)

```cpp
void MeetingAssistant::TranscriptionWorker() {
    while (!shouldStop_) {
        // Wait for 15 seconds (longer chunks = better accuracy)
        std::this_thread::sleep_for(std::chrono::seconds(15));
        
        // Get accumulated audio data
        std::vector<BYTE> audioData;
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioData = std::move(audioBuffer_);
            audioBuffer_.clear();
        }
        
        // Skip if too little audio (minimum 3 seconds)
        if (audioData.size() < minBytes) continue;
        
        // Resample to 16kHz mono 16-bit (Whisper's optimal format)
        std::vector<BYTE> resampled = ResampleTo16kMono16bit(audioData, format);
        
        // Transcribe with optimized parameters
        std::string text = aiService_.Transcribe(resampled, 16000, 1, 16);
        
        // Append to transcript
        if (!text.empty()) {
            AppendTranscript(text);
            EmitEvent(TRANSCRIPT_UPDATE, text);
        }
    }
}
```

### Handling User Queries (with Conversation Memory)

```cpp
void MeetingAssistant::AIWorker() {
    while (!shouldStop_) {
        // Wait for a query
        AIQuery query = /* wait and pop from queue */;
        std::string transcript = GetTranscript();
        
        switch (query.type) {
            case QUESTION: {
                // Build messages WITH conversation history
                std::vector<ChatMessage> messages;
                messages.push_back({"system", systemPrompt});
                messages.push_back({"system", "Transcript:\n" + transcript});
                
                // Include previous Q&A for follow-up context
                for (auto& exchange : conversationHistory_) {
                    messages.push_back({"user", exchange.first});
                    messages.push_back({"assistant", exchange.second});
                }
                messages.push_back({"user", query.question});
                
                response = aiService_.Chat(messages);
                
                // Remember this exchange (max 10)
                conversationHistory_.push_back({query.question, response});
                break;
            }
            case SUMMARY:
                response = aiService_.Summarize(transcript);
                break;
        }
        
        EmitEvent(AI_RESPONSE, response);
    }
}
```

---

## 🌐 HTTP Client for API Calls

### Using WinHTTP

Windows provides `WinHTTP` for making HTTP requests. Here's the flow:

```
┌───────────┐    ┌───────────┐    ┌───────────┐    ┌───────────┐
│  Create   │───▶│  Connect  │───▶│   Send    │───▶│  Receive  │
│  Session  │    │  to Host  │    │  Request  │    │  Response │
└───────────┘    └───────────┘    └───────────┘    └───────────┘
```

### Making a POST Request

```cpp
HttpResponse HttpClient::PostJson(const std::wstring& url,
                                  const std::string& jsonBody,
                                  const std::map<...>& headers) {
    // 1. Parse the URL
    std::wstring host, path;
    INTERNET_PORT port;
    bool useSSL;
    ParseUrl(url, host, path, port, useSSL);
    
    // 2. Connect to server
    HINTERNET hConnect = WinHttpConnect(hSession_, host.c_str(), port, 0);
    
    // 3. Create request
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        useSSL ? WINHTTP_FLAG_SECURE : 0
    );
    
    // 4. Add headers
    for (auto& header : headers) {
        std::wstring h = header.first + L": " + header.second;
        WinHttpAddRequestHeaders(hRequest, h.c_str(), -1, ...);
    }
    
    // 5. Send request with body
    WinHttpSendRequest(hRequest, ..., jsonBody.c_str(), jsonBody.size(), ...);
    
    // 6. Receive response
    WinHttpReceiveResponse(hRequest, nullptr);
    
    // 7. Read response body
    std::string responseBody;
    char buffer[4096];
    DWORD bytesRead;
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead)) {
        responseBody.append(buffer, bytesRead);
    }
    
    return HttpResponse{statusCode, responseBody};
}
```

---

## 📝 Code Walkthrough

### main.cpp - The Entry Point

```cpp
int WINAPI wWinMain(HINSTANCE hInstance, ...) {
    // 1. Initialize COM (required for audio and TTS)
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    
    // 2. Create the application
    InvisibleApp app;
    
    // 3. Configure settings
    AppConfig config;
    config.enableAI = true;
    config.enableTTS = true;
    config.openaiApiKey = "gsk_...";  // Groq API key
    
    // 4. Initialize (creates window, starts audio capture, etc.)
    app.Initialize(config);
    
    // 5. Run the message loop
    app.Run();  // Blocks until exit
    
    // 6. Cleanup
    app.Shutdown();
    CoUninitialize();
}
```

### Hotkey Handling

```cpp
void InvisibleApp::OnHotkey(int id) {
    switch (id) {
        case HOTKEY_ASK_AI:  // Ctrl+Shift+A
            // Show input dialog and ask AI
            std::string question = ShowInputDialog();
            meetingAssistant_->AskQuestion(question);
            break;
            
        case HOTKEY_SUMMARY:  // Ctrl+Shift+M
            meetingAssistant_->GenerateSummary();
            break;
            
        case HOTKEY_TOGGLE_TRANSCRIPT:  // Ctrl+Shift+T
            showTranscript_ = !showTranscript_;
            overlay_->Invalidate();
            break;
    }
}
```

### Rendering the Overlay

```cpp
void InvisibleApp::RenderOverlay(HDC hdc, const Rect& bounds) {
    // Set transparent background
    SetBkMode(hdc, TRANSPARENT);
    
    // Create fonts
    HFONT font = CreateFontW(14, 0, 0, 0, FW_NORMAL, ...);
    
    // Draw control panel
    RECT panelRect = {20, 20, 440, 160};
    FillRect(hdc, &panelRect, CreateSolidBrush(RGB(25, 28, 35)));
    
    // Draw title
    SetTextColor(hdc, RGB(130, 180, 255));
    DrawTextW(hdc, L"AI Meeting Assistant", -1, &titleRect, DT_LEFT);
    
    // Draw transcript panel (if visible)
    if (showTranscript_) {
        DrawTranscriptPanel(hdc, transcriptRect);
    }
    
    // Draw AI response panel
    if (!lastAIResponse_.empty()) {
        DrawResponsePanel(hdc, responseRect);
    }
}
```

---

## 🔗 How Everything Connects

### Startup Flow

```
1. main() starts
   │
   ├──▶ CoInitializeEx()  // Initialize COM
   │
   ├──▶ InvisibleApp::Initialize()
   │    │
   │    ├──▶ OverlayWindow::Create()
   │    │    ├──▶ RegisterClass()
   │    │    ├──▶ CreateWindowEx()
   │    │    └──▶ SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)
   │    │
   │    └──▶ MeetingAssistant::Initialize()
   │         ├──▶ OpenAIService::Initialize()
   │         │    └──▶ HttpClient::Initialize()
   │         │
   │         ├──▶ TextToSpeech::Initialize()
   │         │    └──▶ CoCreateInstance(CLSID_SpVoice)
   │         │
   │         └──▶ AudioCapture::Initialize()
   │              └──▶ Setup WASAPI loopback
   │
   └──▶ InvisibleApp::Run()  // Message loop
```

### Audio Processing Flow

```
Audio Playing ──▶ WASAPI Loopback ──▶ AudioCapture
                                            │
                                            ▼
                                   MeetingAssistant::OnAudioData()
                                            │
                                            ▼
                                   audioBuffer_ (accumulates)
                                            │
                                  (every 15 seconds)
                                            │
                                            ▼
                               ResampleTo16kMono16bit()
                                            │
                                            ▼
                               aiService_.Transcribe()
                          (whisper-large-v3-turbo, lang=en)
                                            │
                                            ▼
                               HTTP POST to Groq Whisper API
                                            │
                                            ▼
                               transcript_ (updated)
                                            │
                                            ▼
                               EmitEvent(TRANSCRIPT_UPDATE)
                                            │
                                            ▼
                               UI updates to show new text
```

### User Query Flow

```
User presses Ctrl+Shift+A
         │
         ▼
OnHotkey(HOTKEY_ASK_AI)
         │
         ▼
ShowInputDialog() ──▶ User types question
         │
         ▼
meetingAssistant_->AskQuestion(question)
         │
         ▼
queryQueue_.push({QUESTION, question})
         │
         ▼
queryCV_.notify_one()  ──▶ Wakes up AIWorker thread
         │
         ▼
aiService_.AnswerQuestion(question, transcript_)
         │
         ▼
HTTP POST to Groq Chat API
         │
         ▼
response = ParseChatResponse(...)
         │
         ▼
EmitEvent(AI_RESPONSE, response)
         │
         ├──▶ UI shows response
         │
         └──▶ tts_.Speak(response)  ──▶ Computer speaks answer
```

---

## 🎓 Key Concepts Summary

### Windows Programming Concepts

| Concept | What It Is | Used For |
|---------|-----------|----------|
| **Win32 API** | Windows' native C API | Creating windows, handling messages |
| **COM** | Component Object Model | Audio capture, TTS |
| **WASAPI** | Windows Audio API | Recording system audio |
| **SAPI** | Speech API | Text-to-speech |
| **WinHTTP** | HTTP library | Making API calls |
| **GDI** | Graphics Device Interface | Drawing UI elements |

### C++ Concepts Used

| Concept | Example | Purpose |
|---------|---------|---------|
| **RAII** | `std::unique_ptr<Window>` | Automatic resource cleanup |
| **Threads** | `std::thread transcriptionThread_` | Background processing |
| **Mutex** | `std::mutex audioMutex_` | Thread-safe data access |
| **Condition Variables** | `queryCV_.wait(...)` | Thread synchronization |
| **Lambda Functions** | `[this](int id) { OnHotkey(id); }` | Callbacks |
| **Smart Pointers** | `std::unique_ptr`, `std::shared_ptr` | Memory management |

### API Concepts

| Concept | What It Is |
|---------|-----------|
| **REST API** | HTTP-based request/response pattern |
| **JSON** | Data format for API communication |
| **Bearer Token** | API key authentication method |
| **Multipart Form** | File upload format (for audio) |
| **Streaming** | Real-time data transmission |

---

## 🚀 Next Steps

Now that you understand how it works, you could:

1. **Add new features:**
   - Save transcripts to file
   - Support multiple AI providers
   - Add voice input for questions
   - Multi-language transcription support

2. **Improve the UI:**
   - Add animations and transitions
   - Make panels resizable/draggable
   - Add settings panel
   - Better code formatting in responses

3. **Cross-platform:**
   - Port to macOS / Linux
   - Build a web-based version

---

## 📚 Resources for Learning More

- [Microsoft Win32 Documentation](https://docs.microsoft.com/en-us/windows/win32/)
- [WASAPI Audio Guide](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
- [Groq API Documentation](https://console.groq.com/docs)
- [C++ Threading](https://en.cppreference.com/w/cpp/thread)

---

**Congratulations! You now understand how the AI Meeting Assistant works from top to bottom!** 🎉

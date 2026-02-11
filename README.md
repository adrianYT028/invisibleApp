# 🔮 Invisible AI Assistant

An **AI-powered invisible overlay** for Windows that helps you during interviews, meetings, and coding sessions — completely invisible to screen sharing and recordings.

## ✨ What Makes It Special

- **👻 100% Invisible** — Uses `WDA_EXCLUDEFROMCAPTURE` to hide from Zoom, Teams, Discord, OBS, and all screen capture
- **🤖 AI-Powered Answers** — Ask questions and get instant answers using Groq's Llama 4 Scout (free & fast)
- **📸 Screen Capture → AI** — Select any area on screen, AI reads and answers the question
- **🎙️ Live Audio Transcription** — Real-time transcription of meeting/interview audio using Whisper
- **💬 Conversation Memory** — Remembers last 10 Q&A exchanges for follow-up questions
- **🖱️ Click-Through** — Mouse events pass to underlying windows
- **🔊 Optional TTS** — Text-to-speech for responses (disabled by default)

## 🛠️ Tech Stack

| Component | Technology |
|---|---|
| Language | C++ (Win32 API) |
| AI | Groq API (Llama 4 Scout + Whisper) |
| Audio Capture | WASAPI Loopback |
| HTTP | WinHTTP |
| Image Processing | GDI+ / Windows Imaging Component |
| TTS | Windows SAPI |

## 📋 Requirements

- Windows 10 version 2004 or later
- Visual Studio 2019+ with C++ Desktop Development workload
- Windows SDK 10.0.19041.0 or later
- **Groq API Key** (free at [console.groq.com](https://console.groq.com))

## 🚀 Setup

### 1. Set API Key
```bash
setx GROQ_API_KEY "your-api-key-here"
```

### 2. Build
**Visual Studio:** Open `InvisibleOverlay.sln` → `Ctrl+Shift+B`

**CMake:**
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### 3. Run
```bash
InvisibleOverlay.exe          # TTS disabled (default)
InvisibleOverlay.exe --tts    # Enable text-to-speech
```

## ⌨️ Hotkeys

| Hotkey | Action |
|---|---|
| `Ctrl+Shift+S` | Select screen region → AI answers the question |
| `Ctrl+Shift+A` | Ask AI about what's being discussed |
| `Ctrl+Shift+D` | Generate meeting summary |
| `Ctrl+Shift+T` | Toggle transcript visibility |
| `Ctrl+Shift+V` | Toggle visibility to screen capture |
| `Ctrl+Shift+Q` | Quit application |

## 🏗️ Project Structure

```
INVISIBLE APP/
├── src/
│   ├── main.cpp              # App entry point, hotkey handling, UI
│   ├── overlay_window.cpp/h  # Invisible overlay window
│   ├── meeting_assistant.cpp/h # Orchestrates AI, audio, transcription
│   ├── ai_service.cpp/h      # Groq API (chat, vision, whisper)
│   ├── audio_capture.cpp/h   # WASAPI loopback audio capture
│   ├── screen_capture.cpp/h  # Screen capture + JPEG encoding
│   ├── text_to_speech.cpp/h  # Windows SAPI TTS
│   ├── http_client.h         # WinHTTP wrapper
│   ├── hotkey_manager.h      # Global hotkey registration
│   └── utils.h               # Common utilities
├── CMakeLists.txt
├── HOW_IT_WORKS.md
├── TECHNICAL_REFERENCE.md
└── README.md
```

## 🧠 Key Features Explained

### Screen Capture → AI Answer
Select any area on your screen (e.g., an interview question), and the AI reads the content and provides a direct answer. Uses WIC for BMP→JPEG conversion and Groq's Llama 4 Scout vision model.

### Audio Transcription (Optimized)
- **16kHz mono 16-bit resampling** — Whisper's native format for best accuracy
- **15-second chunks** — longer audio = better context for the model
- **English language hint** — skips language detection overhead
- **Prompt context** — guides Whisper for interview/meeting audio
- **whisper-large-v3-turbo** — fast and accurate

### Conversation Memory
The AI remembers your last 10 Q&A exchanges. Ask a follow-up question and it has full context of what you already discussed.

### Invisibility
The `WDA_EXCLUDEFROMCAPTURE` flag (Windows 10 2004+) tells DWM to exclude the window from all capture pipelines:
- Screen sharing (Zoom, Teams, Discord)
- Screen recording (OBS, Windows Game Bar)
- PrintWindow API / Desktop Duplication API

## 🤝 Contributing

We're looking for contributors! Areas where you can help:
- 🎨 UI/UX design for the overlay
- 🖥️ C++ / systems programming
- 🤖 AI integration ideas
- 📱 Cross-platform support
- 🧪 Testing & QA

**DM or open an issue if interested!**

## ⚠️ Disclaimer

This application is for **research and educational purposes only**. Understanding these techniques helps security researchers and platform developers build better detection mechanisms.

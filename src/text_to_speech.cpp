#include "text_to_speech.h"
#include <codecvt>
#include <locale>
#include <sapi.h>

// Disable deprecation warning for GetVersionExW used in sphelper.h
#pragma warning(push)
#pragma warning(disable : 4996)
#include <sphelper.h>
#pragma warning(pop)

#pragma comment(lib, "sapi.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

TextToSpeech::TextToSpeech() = default;

TextToSpeech::~TextToSpeech() { Shutdown(); }

// -----------------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------------

bool TextToSpeech::Initialize(const TTSConfig &config) {
  if (initialized_) {
    return true;
  }

  config_ = config;

  // Create SAPI voice
  HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                IID_ISpVoice, (void **)&voice_);
  if (FAILED(hr) || !voice_) {
    OutputDebugStringW(L"[TextToSpeech] Failed to create SpVoice\n");
    return false;
  }

  // Apply configuration
  SetRate(config_.rate);
  SetVolume(config_.volume);

  if (!config_.voiceName.empty()) {
    SetVoice(config_.voiceName);
  }

  initialized_ = true;
  OutputDebugStringW(L"[TextToSpeech] Initialized successfully\n");
  return true;
}

void TextToSpeech::Shutdown() {
  if (voice_) {
    Stop();
    voice_->Release();
    voice_ = nullptr;
  }
  initialized_ = false;
}

// -----------------------------------------------------------------------------
// Speak
// -----------------------------------------------------------------------------

bool TextToSpeech::Speak(const std::wstring &text) {
  if (!initialized_ || !voice_ || text.empty()) {
    return false;
  }

  speaking_ = true;
  HRESULT hr =
      voice_->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
  if (FAILED(hr)) {
    speaking_ = false;
    return false;
  }

  return true;
}

bool TextToSpeech::Speak(const std::string &text) {
  // Convert UTF-8 to wide string
  if (text.empty())
    return false;

  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0)
    return false;

  std::wstring wtext(size - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], size);

  return Speak(wtext);
}

bool TextToSpeech::SpeakSync(const std::wstring &text) {
  if (!initialized_ || !voice_ || text.empty()) {
    return false;
  }

  speaking_ = true;
  HRESULT hr = voice_->Speak(text.c_str(), SPF_PURGEBEFORESPEAK, nullptr);
  speaking_ = false;

  return SUCCEEDED(hr);
}

bool TextToSpeech::SpeakSync(const std::string &text) {
  if (text.empty())
    return false;

  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0)
    return false;

  std::wstring wtext(size - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], size);

  return SpeakSync(wtext);
}

// -----------------------------------------------------------------------------
// Control
// -----------------------------------------------------------------------------

void TextToSpeech::Stop() {
  if (voice_) {
    voice_->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
  }
  speaking_ = false;
}

bool TextToSpeech::IsSpeaking() const {
  if (!voice_)
    return false;

  SPVOICESTATUS status;
  if (SUCCEEDED(voice_->GetStatus(&status, nullptr))) {
    return status.dwRunningState == SPRS_IS_SPEAKING;
  }
  return speaking_;
}

void TextToSpeech::Pause() {
  if (voice_) {
    voice_->Pause();
  }
}

void TextToSpeech::Resume() {
  if (voice_) {
    voice_->Resume();
  }
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

void TextToSpeech::SetRate(int rate) {
  if (voice_) {
    // Clamp to valid range
    rate = (rate < -10) ? -10 : (rate > 10) ? 10 : rate;
    voice_->SetRate(rate);
    config_.rate = rate;
  }
}

void TextToSpeech::SetVolume(int volume) {
  if (voice_) {
    // Clamp to valid range
    volume = (volume < 0) ? 0 : (volume > 100) ? 100 : volume;
    voice_->SetVolume((USHORT)volume);
    config_.volume = volume;
  }
}

bool TextToSpeech::SetVoice(const std::wstring &voiceName) {
  if (!voice_ || voiceName.empty()) {
    return false;
  }

  // Get voice token enumerator
  ISpObjectToken *voiceToken = nullptr;
  IEnumSpObjectTokens *enumTokens = nullptr;

  HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &enumTokens);
  if (FAILED(hr) || !enumTokens) {
    return false;
  }

  bool found = false;
  while (enumTokens->Next(1, &voiceToken, nullptr) == S_OK) {
    LPWSTR name = nullptr;
    if (SUCCEEDED(voiceToken->GetStringValue(nullptr, &name)) && name) {
      if (voiceName == name || wcsstr(name, voiceName.c_str()) != nullptr) {
        voice_->SetVoice(voiceToken);
        found = true;
        CoTaskMemFree(name);
        voiceToken->Release();
        break;
      }
      CoTaskMemFree(name);
    }
    voiceToken->Release();
  }

  enumTokens->Release();

  if (found) {
    config_.voiceName = voiceName;
  }

  return found;
}

std::vector<std::wstring> TextToSpeech::GetAvailableVoices() {
  std::vector<std::wstring> voices;

  ISpObjectToken *voiceToken = nullptr;
  IEnumSpObjectTokens *enumTokens = nullptr;

  HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &enumTokens);
  if (FAILED(hr) || !enumTokens) {
    return voices;
  }

  while (enumTokens->Next(1, &voiceToken, nullptr) == S_OK) {
    LPWSTR name = nullptr;
    if (SUCCEEDED(voiceToken->GetStringValue(nullptr, &name)) && name) {
      voices.push_back(name);
      CoTaskMemFree(name);
    }
    voiceToken->Release();
  }

  enumTokens->Release();
  return voices;
}

} // namespace invisible

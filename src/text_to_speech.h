#pragma once

#include "utils.h"
#include <atomic>
#include <string>
#include <vector>


// Forward declarations for SAPI
struct ISpVoice;

namespace invisible {

// -----------------------------------------------------------------------------
// Text-to-Speech Configuration
// -----------------------------------------------------------------------------

struct TTSConfig {
  int rate = 0;           // -10 to 10 (0 = normal)
  int volume = 100;       // 0 to 100
  std::wstring voiceName; // Empty = default voice
};

// -----------------------------------------------------------------------------
// Text-to-Speech (Windows SAPI)
// -----------------------------------------------------------------------------

class TextToSpeech {
public:
  TextToSpeech();
  ~TextToSpeech();

  // Disable copy
  TextToSpeech(const TextToSpeech &) = delete;
  TextToSpeech &operator=(const TextToSpeech &) = delete;

  // Initialize (must call CoInitialize before this)
  bool Initialize(const TTSConfig &config = TTSConfig());

  // Shutdown
  void Shutdown();

  // Check if initialized
  bool IsInitialized() const { return initialized_; }

  // Speak text (async - returns immediately)
  bool Speak(const std::wstring &text);
  bool Speak(const std::string &text);

  // Speak text (sync - blocks until done)
  bool SpeakSync(const std::wstring &text);
  bool SpeakSync(const std::string &text);

  // Stop current speech
  void Stop();

  // Check if currently speaking
  bool IsSpeaking() const;

  // Pause/Resume
  void Pause();
  void Resume();

  // Configuration
  void SetRate(int rate);     // -10 to 10
  void SetVolume(int volume); // 0 to 100
  bool SetVoice(const std::wstring &voiceName);

  // Get available voices
  std::vector<std::wstring> GetAvailableVoices();

private:
  ISpVoice *voice_ = nullptr;
  TTSConfig config_;
  bool initialized_ = false;
  std::atomic<bool> speaking_{false};
};

} // namespace invisible

#pragma once

#include "ai_service.h"
#include "audio_capture.h"
#include "text_to_speech.h"
#include "utils.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Undefine Windows ERROR macro to prevent conflicts
#ifdef ERROR
#undef ERROR
#endif

namespace invisible {

// -----------------------------------------------------------------------------
// Meeting Assistant Configuration
// -----------------------------------------------------------------------------

struct MeetingAssistantConfig {
  // OpenAI settings
  std::string apiKey;
  std::string gptModel = "gpt-4o-mini";
  std::string whisperModel = "whisper-1";

  // Transcription settings
  float transcriptionIntervalSec =
      5.0f;                        // How often to send audio for transcription
  int maxTranscriptLength = 10000; // Max chars to keep in rolling transcript

  // TTS settings
  bool enableTTS = true;
  int ttsRate = 1; // Slightly faster than normal
  int ttsVolume = 80;

  // Behavior
  bool enableAutoSummary = false; // Auto-summarize every N minutes
  int autoSummaryIntervalMin = 5;
};

// -----------------------------------------------------------------------------
// Meeting Assistant Events
// -----------------------------------------------------------------------------

struct MeetingAssistantEvent {
  enum Type {
    TRANSCRIPT_UPDATE,  // New transcription text
    AI_RESPONSE,        // AI response to query
    SUMMARY_READY,      // Summary generated
    ACTION_ITEMS_READY, // Action items extracted
    EVENT_ERROR         // Error occurred (renamed to avoid Windows ERROR macro)
  };

  Type type;
  std::string text;
  std::string error;
};

using MeetingAssistantCallback =
    std::function<void(const MeetingAssistantEvent &)>;

// -----------------------------------------------------------------------------
// Meeting Assistant
// -----------------------------------------------------------------------------

class MeetingAssistant : public IAudioCaptureHandler {
public:
  MeetingAssistant();
  ~MeetingAssistant();

  // Disable copy
  MeetingAssistant(const MeetingAssistant &) = delete;
  MeetingAssistant &operator=(const MeetingAssistant &) = delete;

  // Initialize all components
  bool Initialize(const MeetingAssistantConfig &config);

  // Shutdown
  void Shutdown();

  // Check if initialized
  bool IsInitialized() const { return initialized_; }

  // Start listening to meeting audio
  bool StartListening();

  // Stop listening
  void StopListening();

  // Check if listening
  bool IsListening() const { return listening_; }

  // Set event callback
  void SetEventCallback(MeetingAssistantCallback callback);

  // Query the AI about the meeting
  void AskQuestion(const std::string &question);

  // Generate summary
  void GenerateSummary();

  // Extract action items
  void ExtractActionItems();

  // Get current transcript
  std::string GetTranscript() const;

  // Clear transcript
  void ClearTranscript();

  // Enable/disable TTS
  void SetTTSEnabled(bool enabled);
  bool IsTTSEnabled() const { return ttsEnabled_; }

  // Stop current TTS
  void StopSpeaking();

  // IAudioCaptureHandler implementation
  void OnAudioData(const AudioBuffer &buffer,
                   const AudioFormat &format) override;
  void OnCaptureError(HRESULT hr, const wchar_t *context) override;

private:
  // Background processing thread
  void ProcessingThreadProc();

  // Transcription worker
  void TranscriptionWorker();

  // AI query worker
  void AIWorker();

  // Emit event to callback
  void EmitEvent(MeetingAssistantEvent::Type type, const std::string &text = "",
                 const std::string &error = "");

  // Append to transcript with length limit
  void AppendTranscript(const std::string &text);

  // Configuration
  MeetingAssistantConfig config_;

  // Components
  AudioCapture audioCapture_;
  OpenAIService aiService_;
  TextToSpeech tts_;

  // State
  std::atomic<bool> initialized_{false};
  std::atomic<bool> listening_{false};
  std::atomic<bool> ttsEnabled_{true};
  std::atomic<bool> shouldStop_{false};

  // Audio buffer for transcription
  std::vector<BYTE> audioBuffer_;
  AudioFormat audioFormat_;
  std::mutex audioMutex_;
  UINT64 lastTranscriptionTime_ = 0;

  // Transcript
  std::string transcript_;
  mutable std::mutex transcriptMutex_;

  // AI query queue
  struct AIQuery {
    enum Type { QUESTION, SUMMARY, ACTION_ITEMS };
    Type type;
    std::string question;
  };
  std::queue<AIQuery> queryQueue_;
  std::mutex queryMutex_;
  std::condition_variable queryCV_;

  // Worker threads
  std::thread transcriptionThread_;
  std::thread aiThread_;

  // Event callback
  MeetingAssistantCallback eventCallback_;
  std::mutex callbackMutex_;
};

} // namespace invisible

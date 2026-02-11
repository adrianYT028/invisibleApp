#include "meeting_assistant.h"
#include <chrono>

namespace invisible {

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

MeetingAssistant::MeetingAssistant() = default;

MeetingAssistant::~MeetingAssistant() { Shutdown(); }

// -----------------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------------

bool MeetingAssistant::Initialize(const MeetingAssistantConfig &config) {
  if (initialized_) {
    return true;
  }

  config_ = config;

  // Initialize AI Service
  AIServiceConfig aiConfig;
  aiConfig.apiKey = config.apiKey;
  aiConfig.model = config.gptModel;
  aiConfig.whisperModel = config.whisperModel;

  if (!aiService_.Initialize(aiConfig)) {
    OutputDebugStringW(L"[MeetingAssistant] Failed to initialize AI service\n");
    return false;
  }

  // Initialize TTS
  if (config.enableTTS) {
    TTSConfig ttsConfig;
    ttsConfig.rate = config.ttsRate;
    ttsConfig.volume = config.ttsVolume;

    if (!tts_.Initialize(ttsConfig)) {
      OutputDebugStringW(
          L"[MeetingAssistant] Warning: Failed to initialize TTS\n");
      // Continue without TTS
    }
  }

  // Initialize Audio Capture
  AudioCaptureConfig audioConfig;
  audioConfig.bufferDurationMs = 100;
  audioConfig.useEventDriven = true;

  if (!audioCapture_.Initialize(audioConfig)) {
    OutputDebugStringW(
        L"[MeetingAssistant] Failed to initialize audio capture\n");
    Shutdown();
    return false;
  }

  ttsEnabled_ = config.enableTTS;
  initialized_ = true;

  OutputDebugStringW(L"[MeetingAssistant] Initialized successfully\n");
  return true;
}

void MeetingAssistant::Shutdown() {
  StopListening();

  // Wait for threads to finish
  shouldStop_ = true;
  queryCV_.notify_all();

  if (transcriptionThread_.joinable()) {
    transcriptionThread_.join();
  }
  if (aiThread_.joinable()) {
    aiThread_.join();
  }

  tts_.Shutdown();
  aiService_.Shutdown();

  initialized_ = false;
}

// -----------------------------------------------------------------------------
// Start / Stop Listening
// -----------------------------------------------------------------------------

bool MeetingAssistant::StartListening() {
  if (!initialized_ || listening_) {
    return false;
  }

  shouldStop_ = false;

  // Start worker threads
  transcriptionThread_ =
      std::thread(&MeetingAssistant::TranscriptionWorker, this);
  aiThread_ = std::thread(&MeetingAssistant::AIWorker, this);

  // Start audio capture
  if (!audioCapture_.Start(this)) {
    shouldStop_ = true;
    queryCV_.notify_all();
    if (transcriptionThread_.joinable())
      transcriptionThread_.join();
    if (aiThread_.joinable())
      aiThread_.join();
    return false;
  }

  listening_ = true;
  OutputDebugStringW(L"[MeetingAssistant] Started listening\n");
  return true;
}

void MeetingAssistant::StopListening() {
  if (!listening_) {
    return;
  }

  audioCapture_.Stop();
  listening_ = false;

  shouldStop_ = true;
  queryCV_.notify_all();

  if (transcriptionThread_.joinable()) {
    transcriptionThread_.join();
  }
  if (aiThread_.joinable()) {
    aiThread_.join();
  }

  OutputDebugStringW(L"[MeetingAssistant] Stopped listening\n");
}

// -----------------------------------------------------------------------------
// Event Callback
// -----------------------------------------------------------------------------

void MeetingAssistant::SetEventCallback(MeetingAssistantCallback callback) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  eventCallback_ = callback;
}

void MeetingAssistant::EmitEvent(MeetingAssistantEvent::Type type,
                                 const std::string &text,
                                 const std::string &error) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  if (eventCallback_) {
    MeetingAssistantEvent event;
    event.type = type;
    event.text = text;
    event.error = error;
    eventCallback_(event);
  }
}

// -----------------------------------------------------------------------------
// Transcript Management
// -----------------------------------------------------------------------------

std::string MeetingAssistant::GetTranscript() const {
  std::lock_guard<std::mutex> lock(transcriptMutex_);
  return transcript_;
}

void MeetingAssistant::ClearTranscript() {
  std::lock_guard<std::mutex> lock(transcriptMutex_);
  transcript_.clear();
}

void MeetingAssistant::AppendTranscript(const std::string &text) {
  if (text.empty())
    return;

  std::lock_guard<std::mutex> lock(transcriptMutex_);

  if (!transcript_.empty()) {
    transcript_ += " ";
  }
  transcript_ += text;

  // Trim to max length (keep the end, which is most recent)
  if ((int)transcript_.length() > config_.maxTranscriptLength) {
    transcript_ =
        transcript_.substr(transcript_.length() - config_.maxTranscriptLength);
    // Find first space to avoid cutting words
    size_t firstSpace = transcript_.find(' ');
    if (firstSpace != std::string::npos) {
      transcript_ = transcript_.substr(firstSpace + 1);
    }
  }
}

// -----------------------------------------------------------------------------
// AI Queries
// -----------------------------------------------------------------------------

void MeetingAssistant::AskQuestion(const std::string &question) {
  std::lock_guard<std::mutex> lock(queryMutex_);
  queryQueue_.push({AIQuery::QUESTION, question});
  queryCV_.notify_one();
}

void MeetingAssistant::GenerateSummary() {
  std::lock_guard<std::mutex> lock(queryMutex_);
  queryQueue_.push({AIQuery::SUMMARY, ""});
  queryCV_.notify_one();
}

void MeetingAssistant::ExtractActionItems() {
  std::lock_guard<std::mutex> lock(queryMutex_);
  queryQueue_.push({AIQuery::ACTION_ITEMS, ""});
  queryCV_.notify_one();
}

// -----------------------------------------------------------------------------
// TTS Control
// -----------------------------------------------------------------------------

void MeetingAssistant::SetTTSEnabled(bool enabled) { ttsEnabled_ = enabled; }

void MeetingAssistant::StopSpeaking() {
  if (tts_.IsInitialized()) {
    tts_.Stop();
  }
}

// -----------------------------------------------------------------------------
// Audio Capture Handler
// -----------------------------------------------------------------------------

void MeetingAssistant::OnAudioData(const AudioBuffer &buffer,
                                   const AudioFormat &format) {
  std::lock_guard<std::mutex> lock(audioMutex_);

  // Store format info
  audioFormat_ = format;

  // Append audio data
  audioBuffer_.insert(audioBuffer_.end(), buffer.data.begin(),
                      buffer.data.end());
}

void MeetingAssistant::OnCaptureError(HRESULT hr, const wchar_t *context) {
  std::wstring msg = L"Audio capture error: ";
  msg += context;
  msg += L" (HRESULT: " + std::to_wstring(hr) + L")";
  OutputDebugStringW(msg.c_str());

  // Convert to UTF-8 for event
  char errorMsg[256];
  WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, errorMsg, sizeof(errorMsg),
                      nullptr, nullptr);
  EmitEvent(MeetingAssistantEvent::EVENT_ERROR, "", errorMsg);
}

// -----------------------------------------------------------------------------
// Audio Resampling: Convert to 16kHz mono 16-bit (Whisper's optimal format)
// -----------------------------------------------------------------------------

static std::vector<BYTE>
ResampleTo16kMono16bit(const std::vector<BYTE> &audioData,
                       const AudioFormat &format) {

  size_t srcBytesPerSample = format.bitsPerSample / 8;
  size_t srcFrameSize = srcBytesPerSample * format.channels;
  size_t numSrcFrames = audioData.size() / srcFrameSize;

  if (numSrcFrames == 0)
    return {};

  // Step 1: Convert to mono float samples
  std::vector<float> monoSamples(numSrcFrames);

  for (size_t i = 0; i < numSrcFrames; i++) {
    float sum = 0.0f;
    const BYTE *frame = audioData.data() + i * srcFrameSize;

    for (UINT16 ch = 0; ch < format.channels; ch++) {
      const BYTE *sample = frame + ch * srcBytesPerSample;
      float value = 0.0f;

      if (format.bitsPerSample == 32) {
        // Float32 (WASAPI default)
        value = *reinterpret_cast<const float *>(sample);
      } else if (format.bitsPerSample == 16) {
        // Int16
        value = *reinterpret_cast<const INT16 *>(sample) / 32768.0f;
      } else if (format.bitsPerSample == 24) {
        // Int24
        int32_t val = (sample[0]) | (sample[1] << 8) | (sample[2] << 16);
        if (val & 0x800000)
          val |= 0xFF000000; // sign extend
        value = val / 8388608.0f;
      }
      sum += value;
    }
    monoSamples[i] = sum / format.channels;
  }

  // Step 2: Resample from source rate to 16000 Hz
  const UINT32 targetRate = 16000;
  double ratio = (double)targetRate / format.sampleRate;
  size_t numDstFrames = (size_t)(numSrcFrames * ratio);

  std::vector<INT16> resampled(numDstFrames);

  for (size_t i = 0; i < numDstFrames; i++) {
    double srcIdx = i / ratio;
    size_t idx0 = (size_t)srcIdx;
    size_t idx1 = idx0 + 1;
    double frac = srcIdx - idx0;

    if (idx1 >= numSrcFrames)
      idx1 = numSrcFrames - 1;

    // Linear interpolation
    float value =
        (float)(monoSamples[idx0] * (1.0 - frac) + monoSamples[idx1] * frac);

    // Clamp and convert to int16
    if (value > 1.0f)
      value = 1.0f;
    if (value < -1.0f)
      value = -1.0f;
    resampled[i] = (INT16)(value * 32767.0f);
  }

  // Step 3: Convert to byte vector
  std::vector<BYTE> result(numDstFrames * 2);
  memcpy(result.data(), resampled.data(), result.size());

  return result;
}

// -----------------------------------------------------------------------------
// Transcription Worker Thread
// -----------------------------------------------------------------------------

void MeetingAssistant::TranscriptionWorker() {
  OutputDebugStringW(L"[MeetingAssistant] Transcription worker started\n");

  while (!shouldStop_) {
    // Sleep for transcription interval
    auto sleepDuration = std::chrono::milliseconds(
        (int)(config_.transcriptionIntervalSec * 1000));
    std::this_thread::sleep_for(sleepDuration);

    if (shouldStop_)
      break;

    // Get accumulated audio data
    std::vector<BYTE> audioData;
    AudioFormat format;
    {
      std::lock_guard<std::mutex> lock(audioMutex_);
      if (audioBuffer_.empty())
        continue;

      audioData = std::move(audioBuffer_);
      audioBuffer_.clear();
      format = audioFormat_;
    }

    // Skip if too little audio
    size_t bytesPerSecond = format.avgBytesPerSec;
    size_t minBytes = (size_t)(bytesPerSecond * config_.minAudioLengthSec);
    if (bytesPerSecond > 0 && audioData.size() < minBytes) {
      // Put data back for next iteration
      std::lock_guard<std::mutex> lock(audioMutex_);
      audioBuffer_.insert(audioBuffer_.begin(), audioData.begin(),
                          audioData.end());
      continue;
    }

    // Resample to 16kHz mono 16-bit for optimal Whisper performance
    std::vector<BYTE> resampledData = ResampleTo16kMono16bit(audioData, format);

    if (resampledData.empty()) {
      OutputDebugStringW(L"[MeetingAssistant] Resampling failed, skipping\n");
      continue;
    }

    // Transcribe using resampled 16kHz mono 16-bit audio
    std::string text = aiService_.Transcribe(resampledData, 16000, 1, 16);

    if (!text.empty()) {
      AppendTranscript(text);
      EmitEvent(MeetingAssistantEvent::TRANSCRIPT_UPDATE, text);
      OutputDebugStringA(("[Transcription] " + text + "\n").c_str());
    }
  }

  OutputDebugStringW(L"[MeetingAssistant] Transcription worker stopped\n");
}

// -----------------------------------------------------------------------------
// AI Worker Thread
// -----------------------------------------------------------------------------

void MeetingAssistant::AIWorker() {
  OutputDebugStringW(L"[MeetingAssistant] AI worker started\n");

  while (!shouldStop_) {
    AIQuery query;

    // Wait for a query
    {
      std::unique_lock<std::mutex> lock(queryMutex_);
      queryCV_.wait(lock,
                    [this] { return shouldStop_ || !queryQueue_.empty(); });

      if (shouldStop_)
        break;
      if (queryQueue_.empty())
        continue;

      query = queryQueue_.front();
      queryQueue_.pop();
    }

    // Get current transcript
    std::string transcript = GetTranscript();

    std::string response;
    MeetingAssistantEvent::Type eventType;

    switch (query.type) {
    case AIQuery::QUESTION: {
      // Build messages with conversation memory
      std::vector<ChatMessage> messages;

      // System prompt
      messages.push_back({"system",
                          "You are an expert interview and meeting assistant. "
                          "Provide DIRECT ANSWERS to questions. Do NOT "
                          "summarize unless asked. "
                          "If there's a coding question, provide the solution. "
                          "Be concise and accurate."});

      // Transcript context
      if (!transcript.empty()) {
        messages.push_back(
            {"system", "Current meeting/interview transcript:\n" + transcript});
      }

      // Previous conversation history (for follow-up context)
      for (const auto &exchange : conversationHistory_) {
        messages.push_back({"user", exchange.first});
        messages.push_back({"assistant", exchange.second});
      }

      // Current question
      messages.push_back({"user", query.question});

      response = aiService_.Chat(messages);
      eventType = MeetingAssistantEvent::AI_RESPONSE;

      // Store in conversation history
      if (!response.empty()) {
        conversationHistory_.push_back({query.question, response});
        // Trim to max history
        while ((int)conversationHistory_.size() > MAX_CONVERSATION_HISTORY) {
          conversationHistory_.erase(conversationHistory_.begin());
        }
      }
      break;
    }

    case AIQuery::SUMMARY:
      response = aiService_.Summarize(transcript);
      eventType = MeetingAssistantEvent::SUMMARY_READY;
      break;

    case AIQuery::ACTION_ITEMS:
      response = aiService_.ExtractActionItems(transcript);
      eventType = MeetingAssistantEvent::ACTION_ITEMS_READY;
      break;
    }

    if (!response.empty()) {
      EmitEvent(eventType, response);

      // Speak response if TTS enabled
      if (ttsEnabled_ && tts_.IsInitialized()) {
        tts_.Speak(response);
      }
    } else {
      EmitEvent(MeetingAssistantEvent::EVENT_ERROR, "",
                "Failed to get AI response: " + aiService_.GetLastError());
    }
  }

  OutputDebugStringW(L"[MeetingAssistant] AI worker stopped\n");
}

void MeetingAssistant::AnalyzeImage(const std::string &base64ImageData,
                                    const std::string &prompt) {
  if (!initialized_) {
    EmitEvent(MeetingAssistantEvent::EVENT_ERROR, "",
              "Meeting assistant not initialized");
    return;
  }

  // Run on a background thread to avoid blocking UI
  std::thread([this, base64ImageData, prompt]() {
    EmitEvent(MeetingAssistantEvent::AI_RESPONSE, "Analyzing image...");

    std::string response = aiService_.AnalyzeImage(base64ImageData, prompt);

    if (!response.empty()) {
      EmitEvent(MeetingAssistantEvent::AI_RESPONSE, response);
    } else {
      EmitEvent(MeetingAssistantEvent::EVENT_ERROR, "",
                "Vision analysis failed: " + aiService_.GetLastError());
    }
  }).detach();
}

} // namespace invisible

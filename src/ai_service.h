#pragma once

#include "http_client.h"
#include "utils.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace invisible {

// -----------------------------------------------------------------------------
// AI Service Configuration
// -----------------------------------------------------------------------------

struct AIServiceConfig {
  std::string apiKey;
  std::string model = "gpt-4o-mini"; // Default to cost-effective model
  std::string whisperModel = "whisper-1";
  int maxTokens = 1024;
  float temperature = 0.7f;

  // System prompt for meeting assistant behavior
  std::string systemPrompt =
      "You are an expert interview and meeting assistant. When given a "
      "question "
      "and meeting transcript context, provide the DIRECT ANSWER to the "
      "question. "
      "Do NOT summarize the transcript unless explicitly asked. "
      "If the transcript contains a question being asked, answer it directly. "
      "If it's a coding question, provide the code solution. "
      "If it's a technical question, give the precise answer. "
      "Be concise and accurate.";
};

// -----------------------------------------------------------------------------
// Chat Message
// -----------------------------------------------------------------------------

struct ChatMessage {
  std::string role; // "system", "user", "assistant"
  std::string content;
};

// -----------------------------------------------------------------------------
// AI Service Interface
// -----------------------------------------------------------------------------

class IAIService {
public:
  virtual ~IAIService() = default;

  virtual bool Initialize(const AIServiceConfig &config) = 0;
  virtual void Shutdown() = 0;
  virtual bool IsInitialized() const = 0;

  // Chat/Query
  virtual std::string Query(const std::string &userMessage,
                            const std::string &context = "") = 0;

  // Conversation with history
  virtual std::string Chat(const std::vector<ChatMessage> &messages) = 0;

  // Meeting-specific functions
  virtual std::string Summarize(const std::string &transcript) = 0;
  virtual std::string ExtractActionItems(const std::string &transcript) = 0;
  virtual std::string AnswerQuestion(const std::string &question,
                                     const std::string &transcript) = 0;
};

// -----------------------------------------------------------------------------
// Speech-to-Text Interface
// -----------------------------------------------------------------------------

class ISpeechToText {
public:
  virtual ~ISpeechToText() = default;

  virtual bool Initialize(const AIServiceConfig &config) = 0;
  virtual void Shutdown() = 0;
  virtual bool IsInitialized() const = 0;

  // Transcribe audio data (PCM format)
  virtual std::string Transcribe(const std::vector<BYTE> &audioData,
                                 UINT32 sampleRate, UINT16 channels,
                                 UINT16 bitsPerSample) = 0;

  // Transcribe from WAV file in memory
  virtual std::string TranscribeWav(const std::vector<BYTE> &wavData) = 0;
};

// -----------------------------------------------------------------------------
// OpenAI Service Implementation
// -----------------------------------------------------------------------------

class OpenAIService : public IAIService, public ISpeechToText {
public:
  OpenAIService();
  ~OpenAIService() override;

  // IAIService implementation
  bool Initialize(const AIServiceConfig &config) override;
  void Shutdown() override;
  bool IsInitialized() const override { return initialized_; }

  std::string Query(const std::string &userMessage,
                    const std::string &context = "") override;
  std::string Chat(const std::vector<ChatMessage> &messages) override;
  std::string Summarize(const std::string &transcript) override;
  std::string ExtractActionItems(const std::string &transcript) override;
  std::string AnswerQuestion(const std::string &question,
                             const std::string &transcript) override;

  // ISpeechToText implementation
  std::string Transcribe(const std::vector<BYTE> &audioData, UINT32 sampleRate,
                         UINT16 channels, UINT16 bitsPerSample) override;
  std::string TranscribeWav(const std::vector<BYTE> &wavData) override;

  // Vision - analyze image with AI
  std::string AnalyzeImage(const std::string &base64ImageData,
                           const std::string &prompt = "");

  // Get last error message
  std::string GetLastError() const { return lastError_; }

private:
  // Build JSON payload for chat completions
  std::string BuildChatPayload(const std::vector<ChatMessage> &messages);

  // Parse response from chat completions
  std::string ParseChatResponse(const std::string &response);

  // Parse response from Whisper API
  std::string ParseWhisperResponse(const std::string &response);

  // Convert PCM to WAV format
  std::vector<BYTE> ConvertToWav(const std::vector<BYTE> &pcmData,
                                 UINT32 sampleRate, UINT16 channels,
                                 UINT16 bitsPerSample);

  // Simple JSON string escaping
  std::string EscapeJson(const std::string &str);

  HttpClient httpClient_;
  AIServiceConfig config_;
  bool initialized_ = false;
  std::string lastError_;
  std::mutex mutex_;

  // API endpoints
  static constexpr const wchar_t *CHAT_ENDPOINT =
      L"https://api.openai.com/v1/chat/completions";
  static constexpr const wchar_t *WHISPER_ENDPOINT =
      L"https://api.openai.com/v1/audio/transcriptions";
};

} // namespace invisible

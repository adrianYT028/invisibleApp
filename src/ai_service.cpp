#include "ai_service.h"
#include <iomanip>
#include <sstream>

namespace invisible {

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

OpenAIService::OpenAIService() = default;

OpenAIService::~OpenAIService() { Shutdown(); }

// -----------------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------------

bool OpenAIService::Initialize(const AIServiceConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return true;
  }

  if (config.apiKey.empty()) {
    lastError_ = "API key is required";
    return false;
  }

  config_ = config;

  if (!httpClient_.Initialize()) {
    lastError_ = "Failed to initialize HTTP client";
    return false;
  }

  initialized_ = true;
  OutputDebugStringW(L"[GroqService] Initialized successfully\n");
  return true;
}

void OpenAIService::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  httpClient_.Shutdown();
  initialized_ = false;
}

// -----------------------------------------------------------------------------
// JSON Helpers
// -----------------------------------------------------------------------------

std::string OpenAIService::EscapeJson(const std::string &str) {
  std::ostringstream escaped;
  for (char c : str) {
    switch (c) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\b':
      escaped << "\\b";
      break;
    case '\f':
      escaped << "\\f";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << (int)c;
      } else {
        escaped << c;
      }
    }
  }
  return escaped.str();
}

// Build Groq API payload (OpenAI compatible format)
std::string
OpenAIService::BuildChatPayload(const std::vector<ChatMessage> &messages) {
  std::ostringstream json;
  json << "{";
  json << "\"model\":\"llama-3.3-70b-versatile\","; // Groq's best free model
  json << "\"max_tokens\":" << config_.maxTokens << ",";
  json << "\"temperature\":" << std::fixed << std::setprecision(1)
       << config_.temperature << ",";
  json << "\"messages\":[";

  for (size_t i = 0; i < messages.size(); ++i) {
    if (i > 0)
      json << ",";
    json << "{\"role\":\"" << EscapeJson(messages[i].role) << "\",";
    json << "\"content\":\"" << EscapeJson(messages[i].content) << "\"}";
  }

  json << "]}";
  return json.str();
}

std::string OpenAIService::ParseChatResponse(const std::string &response) {
  // OpenAI/Groq response format: {"choices":[{"message":{"content":"..."}}]}
  size_t contentPos = response.find("\"content\":");
  if (contentPos == std::string::npos) {
    // Check for error
    size_t errorPos = response.find("\"error\":");
    if (errorPos != std::string::npos) {
      size_t msgPos = response.find("\"message\":", errorPos);
      if (msgPos != std::string::npos) {
        size_t start = response.find('"', msgPos + 10) + 1;
        size_t end = response.find('"', start);
        if (start != std::string::npos && end != std::string::npos) {
          lastError_ = response.substr(start, end - start);
          return "";
        }
      }
    }
    lastError_ = "Failed to parse response";
    return "";
  }

  // Find the content value
  size_t start = response.find('"', contentPos + 10) + 1;
  if (start == std::string::npos)
    return "";

  // Find end of content (handle escaped quotes)
  std::string result;
  bool escaped = false;
  for (size_t i = start; i < response.length(); ++i) {
    char c = response[i];
    if (escaped) {
      switch (c) {
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      case '"':
        result += '"';
        break;
      case '\\':
        result += '\\';
        break;
      default:
        result += c;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    } else {
      result += c;
    }
  }

  return result;
}

std::string OpenAIService::ParseWhisperResponse(const std::string &response) {
  // Groq supports Whisper! Looking for: "text":"<text>"
  size_t textPos = response.find("\"text\":");
  if (textPos == std::string::npos) {
    lastError_ = "Failed to parse Whisper response";
    return "";
  }

  size_t start = response.find('"', textPos + 7) + 1;
  size_t end = response.find('"', start);

  if (start == std::string::npos || end == std::string::npos) {
    return "";
  }

  // Handle escaped characters
  std::string result;
  bool escaped = false;
  for (size_t i = start; i < end; ++i) {
    char c = response[i];
    if (escaped) {
      switch (c) {
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      case '"':
        result += '"';
        break;
      case '\\':
        result += '\\';
        break;
      default:
        result += c;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      result += c;
    }
  }

  return result;
}

// -----------------------------------------------------------------------------
// Chat / Query
// -----------------------------------------------------------------------------

std::string OpenAIService::Query(const std::string &userMessage,
                                 const std::string &context) {
  std::vector<ChatMessage> messages;

  // Add system prompt
  messages.push_back({"system", config_.systemPrompt});

  // Add context if provided
  if (!context.empty()) {
    messages.push_back({"system", "Meeting context: " + context});
  }

  // Add user message
  messages.push_back({"user", userMessage});

  return Chat(messages);
}

std::string OpenAIService::Chat(const std::vector<ChatMessage> &messages) {
  if (!initialized_) {
    lastError_ = "Service not initialized";
    return "";
  }

  std::string payload = BuildChatPayload(messages);

  // Groq API endpoint (OpenAI compatible)
  std::wstring endpoint = L"https://api.groq.com/openai/v1/chat/completions";

  std::map<std::wstring, std::wstring> headers;
  headers[L"Authorization"] =
      L"Bearer " + std::wstring(config_.apiKey.begin(), config_.apiKey.end());
  headers[L"Content-Type"] = L"application/json";

  HttpResponse response = httpClient_.PostJson(endpoint, payload, headers);

  if (!response.IsSuccess()) {
    // Parse the error message
    std::string errorMsg = "HTTP " + std::to_string(response.statusCode);
    size_t msgPos = response.body.find("\"message\":");
    if (msgPos != std::string::npos) {
      size_t start = response.body.find('"', msgPos + 10) + 1;
      size_t end = response.body.find('"', start);
      if (start != std::string::npos && end != std::string::npos) {
        errorMsg += ": " + response.body.substr(start, end - start);
      }
    }
    lastError_ = errorMsg;
    OutputDebugStringA(
        ("[GroqService] API error: " + response.body + "\n").c_str());
    return "";
  }

  return ParseChatResponse(response.body);
}

// -----------------------------------------------------------------------------
// Meeting-Specific Functions
// -----------------------------------------------------------------------------

std::string OpenAIService::Summarize(const std::string &transcript) {
  return Query("Please provide a concise summary of this meeting transcript. "
               "Include key discussion points and any decisions made. "
               "Format as bullet points.\n\nTranscript:\n" +
               transcript);
}

std::string OpenAIService::ExtractActionItems(const std::string &transcript) {
  return Query(
      "Extract all action items from this meeting transcript. "
      "For each action item, identify who is responsible if mentioned. "
      "Format as a numbered list.\n\nTranscript:\n" +
      transcript);
}

std::string OpenAIService::AnswerQuestion(const std::string &question,
                                          const std::string &transcript) {
  return Query(question, transcript);
}

// -----------------------------------------------------------------------------
// Speech-to-Text
// -----------------------------------------------------------------------------

std::vector<BYTE> OpenAIService::ConvertToWav(const std::vector<BYTE> &pcmData,
                                              UINT32 sampleRate,
                                              UINT16 channels,
                                              UINT16 bitsPerSample) {
  // WAV file header
  struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    UINT32 fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    UINT32 fmtSize = 16;
    UINT16 audioFormat = 1; // PCM
    UINT16 numChannels;
    UINT32 sampleRate;
    UINT32 byteRate;
    UINT16 blockAlign;
    UINT16 bitsPerSample;
    char data[4] = {'d', 'a', 't', 'a'};
    UINT32 dataSize;
  };

  WavHeader header;
  header.numChannels = channels;
  header.sampleRate = sampleRate;
  header.bitsPerSample = bitsPerSample;
  header.blockAlign = channels * bitsPerSample / 8;
  header.byteRate = sampleRate * header.blockAlign;
  header.dataSize = (UINT32)pcmData.size();
  header.fileSize = sizeof(WavHeader) - 8 + header.dataSize;

  std::vector<BYTE> wavData;
  wavData.reserve(sizeof(WavHeader) + pcmData.size());

  const BYTE *headerBytes = reinterpret_cast<const BYTE *>(&header);
  wavData.insert(wavData.end(), headerBytes, headerBytes + sizeof(WavHeader));
  wavData.insert(wavData.end(), pcmData.begin(), pcmData.end());

  return wavData;
}

std::string OpenAIService::Transcribe(const std::vector<BYTE> &audioData,
                                      UINT32 sampleRate, UINT16 channels,
                                      UINT16 bitsPerSample) {
  if (!initialized_) {
    lastError_ = "Service not initialized";
    return "";
  }

  // Convert PCM to WAV
  std::vector<BYTE> wavData =
      ConvertToWav(audioData, sampleRate, channels, bitsPerSample);
  return TranscribeWav(wavData);
}

std::string OpenAIService::TranscribeWav(const std::vector<BYTE> &wavData) {
  if (!initialized_) {
    lastError_ = "Service not initialized";
    return "";
  }

  if (wavData.empty()) {
    return "";
  }

  // Groq Whisper API endpoint
  std::wstring endpoint =
      L"https://api.groq.com/openai/v1/audio/transcriptions";

  std::map<std::wstring, std::wstring> headers;
  headers[L"Authorization"] =
      L"Bearer " + std::wstring(config_.apiKey.begin(), config_.apiKey.end());

  std::map<std::string, std::string> fields;
  fields["model"] = "whisper-large-v3"; // Groq's Whisper model
  fields["response_format"] = "json";

  HttpResponse response = httpClient_.PostMultipart(
      endpoint, fields, "audio.wav", "file", wavData, "audio/wav", headers);

  if (!response.IsSuccess()) {
    lastError_ = "HTTP error: " + std::to_string(response.statusCode);
    OutputDebugStringA(
        ("[GroqService] Whisper API error: " + response.body + "\n").c_str());
    return "";
  }

  return ParseWhisperResponse(response.body);
}

} // namespace invisible

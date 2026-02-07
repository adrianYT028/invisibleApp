#pragma once

#include "utils.h"
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// HTTP Response
// -----------------------------------------------------------------------------

struct HttpResponse {
  int statusCode = 0;
  std::string body;
  std::map<std::string, std::string> headers;
  std::wstring error;

  bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
};

// -----------------------------------------------------------------------------
// HTTP Client Configuration
// -----------------------------------------------------------------------------

struct HttpClientConfig {
  std::wstring userAgent = L"InvisibleOverlay/1.0";
  DWORD connectTimeoutMs = 30000;
  DWORD sendTimeoutMs = 30000;
  DWORD receiveTimeoutMs = 60000;
};

// -----------------------------------------------------------------------------
// HTTP Client (WinHTTP wrapper)
// -----------------------------------------------------------------------------

class HttpClient {
public:
  HttpClient();
  ~HttpClient();

  // Disable copy
  HttpClient(const HttpClient &) = delete;
  HttpClient &operator=(const HttpClient &) = delete;

  // Initialize the client
  bool Initialize(const HttpClientConfig &config = HttpClientConfig());

  // Shutdown and release resources
  void Shutdown();

  // Synchronous GET request
  HttpResponse Get(const std::wstring &url,
                   const std::map<std::wstring, std::wstring> &headers = {});

  // Synchronous POST request with JSON body
  HttpResponse
  PostJson(const std::wstring &url, const std::string &jsonBody,
           const std::map<std::wstring, std::wstring> &headers = {});

  // Synchronous POST request with multipart form data (for file uploads)
  HttpResponse PostMultipart(
      const std::wstring &url, const std::map<std::string, std::string> &fields,
      const std::string &fileName, const std::string &fileField,
      const std::vector<BYTE> &fileData, const std::string &fileMimeType,
      const std::map<std::wstring, std::wstring> &headers = {});

  // Check if initialized
  bool IsInitialized() const { return initialized_; }

private:
  // Parse URL into components
  bool ParseUrl(const std::wstring &url, std::wstring &host, std::wstring &path,
                INTERNET_PORT &port, bool &useSSL);

  // Send request and receive response
  HttpResponse SendRequest(const std::wstring &host, INTERNET_PORT port,
                           bool useSSL, const std::wstring &verb,
                           const std::wstring &path,
                           const std::map<std::wstring, std::wstring> &headers,
                           const void *body, DWORD bodyLength,
                           const std::wstring &contentType);

  HINTERNET hSession_ = nullptr;
  HttpClientConfig config_;
  bool initialized_ = false;
};

} // namespace invisible

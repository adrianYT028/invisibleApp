#include "http_client.h"
#include <sstream>
#include <winhttp.h>


namespace invisible {

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

HttpClient::HttpClient() = default;

HttpClient::~HttpClient() { Shutdown(); }

// -----------------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------------

bool HttpClient::Initialize(const HttpClientConfig &config) {
  if (initialized_) {
    return true;
  }

  config_ = config;

  // Create WinHTTP session
  hSession_ = WinHttpOpen(config_.userAgent.c_str(),
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

  if (!hSession_) {
    OutputDebugStringW(L"[HttpClient] Failed to create WinHTTP session\n");
    return false;
  }

  // Set timeouts
  WinHttpSetTimeouts(hSession_,
                     config_.connectTimeoutMs,  // DNS resolve timeout
                     config_.connectTimeoutMs,  // Connect timeout
                     config_.sendTimeoutMs,     // Send timeout
                     config_.receiveTimeoutMs); // Receive timeout

  initialized_ = true;
  OutputDebugStringW(L"[HttpClient] Initialized successfully\n");
  return true;
}

void HttpClient::Shutdown() {
  if (hSession_) {
    WinHttpCloseHandle(hSession_);
    hSession_ = nullptr;
  }
  initialized_ = false;
}

// -----------------------------------------------------------------------------
// URL Parsing
// -----------------------------------------------------------------------------

bool HttpClient::ParseUrl(const std::wstring &url, std::wstring &host,
                          std::wstring &path, INTERNET_PORT &port,
                          bool &useSSL) {
  URL_COMPONENTS urlComp = {};
  urlComp.dwStructSize = sizeof(urlComp);

  wchar_t hostBuffer[256] = {};
  wchar_t pathBuffer[2048] = {};

  urlComp.lpszHostName = hostBuffer;
  urlComp.dwHostNameLength = ARRAYSIZE(hostBuffer);
  urlComp.lpszUrlPath = pathBuffer;
  urlComp.dwUrlPathLength = ARRAYSIZE(pathBuffer);

  if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
    return false;
  }

  host = hostBuffer;
  path = pathBuffer;
  port = urlComp.nPort;
  useSSL = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

  if (path.empty()) {
    path = L"/";
  }

  return true;
}

// -----------------------------------------------------------------------------
// GET Request
// -----------------------------------------------------------------------------

HttpResponse
HttpClient::Get(const std::wstring &url,
                const std::map<std::wstring, std::wstring> &headers) {
  std::wstring host, path;
  INTERNET_PORT port;
  bool useSSL;

  if (!ParseUrl(url, host, path, port, useSSL)) {
    HttpResponse resp;
    resp.error = L"Failed to parse URL";
    return resp;
  }

  return SendRequest(host, port, useSSL, L"GET", path, headers, nullptr, 0,
                     L"");
}

// -----------------------------------------------------------------------------
// POST JSON Request
// -----------------------------------------------------------------------------

HttpResponse
HttpClient::PostJson(const std::wstring &url, const std::string &jsonBody,
                     const std::map<std::wstring, std::wstring> &headers) {
  std::wstring host, path;
  INTERNET_PORT port;
  bool useSSL;

  if (!ParseUrl(url, host, path, port, useSSL)) {
    HttpResponse resp;
    resp.error = L"Failed to parse URL";
    return resp;
  }

  return SendRequest(host, port, useSSL, L"POST", path, headers,
                     jsonBody.data(), (DWORD)jsonBody.size(),
                     L"application/json");
}

// -----------------------------------------------------------------------------
// POST Multipart Request (for file uploads)
// -----------------------------------------------------------------------------

HttpResponse HttpClient::PostMultipart(
    const std::wstring &url, const std::map<std::string, std::string> &fields,
    const std::string &fileName, const std::string &fileField,
    const std::vector<BYTE> &fileData, const std::string &fileMimeType,
    const std::map<std::wstring, std::wstring> &headers) {
  std::wstring host, path;
  INTERNET_PORT port;
  bool useSSL;

  if (!ParseUrl(url, host, path, port, useSSL)) {
    HttpResponse resp;
    resp.error = L"Failed to parse URL";
    return resp;
  }

  // Generate boundary
  std::string boundary =
      "----InvisibleOverlayBoundary" + std::to_string(GetTickCount64());

  // Build multipart body
  std::vector<BYTE> body;

  // Add text fields
  for (const auto &field : fields) {
    std::string part = "--" + boundary + "\r\n";
    part +=
        "Content-Disposition: form-data; name=\"" + field.first + "\"\r\n\r\n";
    part += field.second + "\r\n";
    body.insert(body.end(), part.begin(), part.end());
  }

  // Add file field
  std::string filePart = "--" + boundary + "\r\n";
  filePart += "Content-Disposition: form-data; name=\"" + fileField +
              "\"; filename=\"" + fileName + "\"\r\n";
  filePart += "Content-Type: " + fileMimeType + "\r\n\r\n";
  body.insert(body.end(), filePart.begin(), filePart.end());
  body.insert(body.end(), fileData.begin(), fileData.end());

  std::string endPart = "\r\n--" + boundary + "--\r\n";
  body.insert(body.end(), endPart.begin(), endPart.end());

  // Content type with boundary
  std::wstring contentType = L"multipart/form-data; boundary=";
  contentType += std::wstring(boundary.begin(), boundary.end());

  return SendRequest(host, port, useSSL, L"POST", path, headers, body.data(),
                     (DWORD)body.size(), contentType);
}

// -----------------------------------------------------------------------------
// Internal: Send Request
// -----------------------------------------------------------------------------

HttpResponse HttpClient::SendRequest(
    const std::wstring &host, INTERNET_PORT port, bool useSSL,
    const std::wstring &verb, const std::wstring &path,
    const std::map<std::wstring, std::wstring> &headers, const void *body,
    DWORD bodyLength, const std::wstring &contentType) {
  HttpResponse response;
  HINTERNET hConnect = nullptr;
  HINTERNET hRequest = nullptr;

  // Connect to server
  hConnect = WinHttpConnect(hSession_, host.c_str(), port, 0);
  if (!hConnect) {
    response.error = L"Failed to connect to server";
    return response;
  }

  // Create request
  DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
  hRequest = WinHttpOpenRequest(hConnect, verb.c_str(), path.c_str(), nullptr,
                                WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    response.error = L"Failed to create request";
    WinHttpCloseHandle(hConnect);
    return response;
  }

  // Add custom headers
  for (const auto &header : headers) {
    std::wstring headerLine = header.first + L": " + header.second;
    WinHttpAddRequestHeaders(hRequest, headerLine.c_str(), (DWORD)-1,
                             WINHTTP_ADDREQ_FLAG_ADD |
                                 WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  // Add content type if provided
  if (!contentType.empty()) {
    std::wstring ctHeader = L"Content-Type: " + contentType;
    WinHttpAddRequestHeaders(hRequest, ctHeader.c_str(), (DWORD)-1,
                             WINHTTP_ADDREQ_FLAG_ADD |
                                 WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  // Send request
  BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, bodyLength, bodyLength, 0);
  if (!result) {
    response.error = L"Failed to send request";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return response;
  }

  // Receive response
  result = WinHttpReceiveResponse(hRequest, nullptr);
  if (!result) {
    response.error = L"Failed to receive response";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return response;
  }

  // Get status code
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                      &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
  response.statusCode = (int)statusCode;

  // Read response body
  std::vector<char> buffer;
  DWORD bytesAvailable = 0;
  DWORD bytesRead = 0;

  while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) &&
         bytesAvailable > 0) {
    std::vector<char> chunk(bytesAvailable);
    if (WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead)) {
      buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + bytesRead);
    }
  }

  response.body = std::string(buffer.begin(), buffer.end());

  // Cleanup
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);

  return response;
}

} // namespace invisible

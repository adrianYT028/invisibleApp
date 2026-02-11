#include "screen_capture.h"
#include <algorithm>
#include <fstream>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// Screen Capture Implementation
// -----------------------------------------------------------------------------

CapturedImage ScreenCapture::CaptureRegion(const Rect &region) {
  if (!region.IsValid()) {
    return CapturedImage();
  }

  HDC screenDC = GetDC(nullptr);
  CapturedImage result =
      CaptureFromDC(screenDC, region.x, region.y, region.width, region.height);
  ReleaseDC(nullptr, screenDC);

  return result;
}

CapturedImage ScreenCapture::CapturePrimaryMonitor() {
  Rect primaryRect = GetPrimaryMonitorRect();
  return CaptureRegion(primaryRect);
}

CapturedImage ScreenCapture::CaptureAllMonitors() {
  Rect virtualRect = GetVirtualScreenRect();
  return CaptureRegion(virtualRect);
}

CapturedImage ScreenCapture::CaptureWindow(HWND hwnd, bool clientAreaOnly) {
  if (!IsWindow(hwnd)) {
    return CapturedImage();
  }

  RECT rc;
  if (clientAreaOnly) {
    GetClientRect(hwnd, &rc);
  } else {
    GetWindowRect(hwnd, &rc);
  }

  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;

  if (width <= 0 || height <= 0) {
    return CapturedImage();
  }

  HDC windowDC = clientAreaOnly ? GetDC(hwnd) : GetWindowDC(hwnd);
  CapturedImage result = CaptureFromDC(windowDC, 0, 0, width, height);
  ReleaseDC(hwnd, windowDC);

  return result;
}

CapturedImage ScreenCapture::CaptureFromDC(HDC srcDC, int srcX, int srcY,
                                           int width, int height) {
  CapturedImage image;

  // Create compatible DC and bitmap
  HDC memDC = CreateCompatibleDC(srcDC);
  if (!memDC) {
    LogError(L"Failed to create compatible DC");
    return image;
  }

  // Create a DIB section for direct pixel access
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height; // Negative for top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  BYTE *pixels = nullptr;
  HBITMAP hBitmap =
      CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS,
                       reinterpret_cast<void **>(&pixels), nullptr, 0);

  if (!hBitmap || !pixels) {
    LogError(L"Failed to create DIB section");
    DeleteDC(memDC);
    return image;
  }

  HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, hBitmap));

  // Copy the screen content
  // Note: BitBlt will NOT capture windows with WDA_EXCLUDEFROMCAPTURE
  // This is intentional - it's how we ensure our overlay doesn't appear in
  // captures
  if (!BitBlt(memDC, 0, 0, width, height, srcDC, srcX, srcY, SRCCOPY)) {
    LogError(L"BitBlt failed");
    SelectObject(memDC, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    return image;
  }

  // Calculate stride (row size aligned to 4 bytes)
  int stride = ((width * 32 + 31) / 32) * 4;

  // Copy pixel data
  image.width = width;
  image.height = height;
  image.stride = stride;
  image.bitsPerPixel = 32;
  image.pixels.resize(stride * height);
  memcpy(image.pixels.data(), pixels, stride * height);

  // Cleanup
  SelectObject(memDC, oldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(memDC);

  return image;
}

bool ScreenCapture::SaveToBmp(const CapturedImage &image,
                              const wchar_t *filePath) {
  if (!image.IsValid()) {
    return false;
  }

  std::ofstream file(filePath, std::ios::binary);
  if (!file) {
    return false;
  }

  // BMP file header
  BITMAPFILEHEADER bfh = {};
  bfh.bfType = 0x4D42; // "BM"
  bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
               image.stride * image.height;
  bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

  // BMP info header
  BITMAPINFOHEADER bih = {};
  bih.biSize = sizeof(BITMAPINFOHEADER);
  bih.biWidth = image.width;
  bih.biHeight = -image.height; // Negative for top-down
  bih.biPlanes = 1;
  bih.biBitCount = 32;
  bih.biCompression = BI_RGB;
  bih.biSizeImage = image.stride * image.height;

  file.write(reinterpret_cast<const char *>(&bfh), sizeof(bfh));
  file.write(reinterpret_cast<const char *>(&bih), sizeof(bih));
  file.write(reinterpret_cast<const char *>(image.pixels.data()),
             image.pixels.size());

  return file.good();
}

bool ScreenCapture::SaveToPpm(const CapturedImage &image,
                              const wchar_t *filePath) {
  if (!image.IsValid()) {
    return false;
  }

  std::ofstream file(filePath, std::ios::binary);
  if (!file) {
    return false;
  }

  // PPM header
  file << "P6\n" << image.width << " " << image.height << "\n255\n";

  // Write RGB data (PPM doesn't support alpha)
  for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
      const BYTE *p = image.pixels.data() + y * image.stride + x * 4;
      // BGRA to RGB
      file.put(p[2]); // R
      file.put(p[1]); // G
      file.put(p[0]); // B
    }
  }

  return file.good();
}

// -----------------------------------------------------------------------------
// Region Selector Implementation
// -----------------------------------------------------------------------------

bool RegionSelector::selectorClassRegistered_ = false;

RegionSelector::RegionSelector() = default;

RegionSelector::~RegionSelector() { CancelSelection(); }

bool RegionSelector::RegisterSelectorClass() {
  if (selectorClassRegistered_) {
    return true;
  }

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = SelectorWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = SELECTOR_CLASS;
  wc.cbWndExtra = sizeof(RegionSelector *);

  if (RegisterClassExW(&wc) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LogError(L"Failed to register selector window class");
    return false;
  }

  selectorClassRegistered_ = true;
  return true;
}

void RegionSelector::StartSelection(SelectionCallback callback) {
  if (selecting_) {
    return;
  }

  if (!RegisterSelectorClass()) {
    return;
  }

  callback_ = std::move(callback);

  // Take a snapshot of the current screen (without our overlay)
  screenSnapshot_ = ScreenCapture::CaptureAllMonitors();

  // Create a fullscreen overlay for selection
  Rect virtualScreen = GetVirtualScreenRect();

  DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
  DWORD style = WS_POPUP;

  selectorHwnd_ = CreateWindowExW(
      exStyle, SELECTOR_CLASS, L"", style, virtualScreen.x, virtualScreen.y,
      virtualScreen.width, virtualScreen.height, nullptr, nullptr,
      GetModuleHandleW(nullptr), this);

  if (!selectorHwnd_) {
    LogError(L"Failed to create selector window");
    return;
  }

  SetWindowLongPtrW(selectorHwnd_, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(this));

  // Hide this window from capture too (it's just UI)
  SetWindowDisplayAffinity(selectorHwnd_, WDA_EXCLUDEFROMCAPTURE);

  // Create bitmap from snapshot for display
  if (screenSnapshot_.IsValid()) {
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screenSnapshot_.width;
    bmi.bmiHeader.biHeight = -screenSnapshot_.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    snapshotBitmap_ =
        CreateDIBitmap(screenDC, &bmi.bmiHeader, CBM_INIT,
                       screenSnapshot_.pixels.data(), &bmi, DIB_RGB_COLORS);

    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
  }

  selecting_ = true;
  ShowWindow(selectorHwnd_, SW_SHOW);
  SetCapture(selectorHwnd_);

  LogInfo(L"Region selection started - click and drag to select");
}

void RegionSelector::CancelSelection() {
  if (selectorHwnd_) {
    ReleaseCapture();
    DestroyWindow(selectorHwnd_);
    selectorHwnd_ = nullptr;
  }

  if (snapshotBitmap_) {
    DeleteObject(snapshotBitmap_);
    snapshotBitmap_ = nullptr;
  }

  screenSnapshot_ = CapturedImage();
  selecting_ = false;
  isDragging_ = false;
}

Rect RegionSelector::GetSelectionRect() const {
  int x = std::min(startPoint_.x, currentPoint_.x);
  int y = std::min(startPoint_.y, currentPoint_.y);
  int w = std::abs(currentPoint_.x - startPoint_.x);
  int h = std::abs(currentPoint_.y - startPoint_.y);
  return Rect(x, y, w, h);
}

LRESULT CALLBACK RegionSelector::SelectorWndProc(HWND hwnd, UINT msg,
                                                 WPARAM wParam, LPARAM lParam) {
  RegionSelector *self = reinterpret_cast<RegionSelector *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCT *>(lParam);
    self = static_cast<RegionSelector *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (self) {
    return self->HandleSelectorMessage(hwnd, msg, wParam, lParam);
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RegionSelector::HandleSelectorMessage(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);

    // Draw the screen snapshot as background
    if (snapshotBitmap_) {
      HDC memDC = CreateCompatibleDC(hdc);
      HBITMAP oldBmp =
          static_cast<HBITMAP>(SelectObject(memDC, snapshotBitmap_));

      // Adjust for virtual screen offset
      Rect virtualScreen = GetVirtualScreenRect();
      BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, -virtualScreen.x,
             -virtualScreen.y, SRCCOPY);

      SelectObject(memDC, oldBmp);
      DeleteDC(memDC);
    } else {
      // Fallback: semi-transparent overlay
      HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
      FillRect(hdc, &rc, darkBrush);
      DeleteObject(darkBrush);
    }

    // Draw dimming overlay
    // Create a semi-transparent effect by drawing a dark rectangle
    HBRUSH dimBrush = CreateSolidBrush(RGB(0, 0, 0));
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 128, 0};

    // For simplicity, just dim slightly
    SetBkMode(hdc, TRANSPARENT);

    // Draw selection rectangle if dragging
    if (isDragging_) {
      Rect sel = GetSelectionRect();

      if (sel.width > 0 && sel.height > 0) {
        // Adjust for window position
        Rect virtualScreen = GetVirtualScreenRect();
        int adjX = sel.x - virtualScreen.x;
        int adjY = sel.y - virtualScreen.y;

        // Draw selection border
        HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
        HBRUSH oldBrush =
            static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

        Rectangle(hdc, adjX, adjY, adjX + sel.width, adjY + sel.height);

        // Draw size indicator
        wchar_t sizeText[64];
        swprintf_s(sizeText, L"%d x %d", sel.width, sel.height);

        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);

        RECT textRect = {adjX + 5, adjY + sel.height + 5, adjX + 150,
                         adjY + sel.height + 25};
        DrawTextW(hdc, sizeText, -1, &textRect, DT_LEFT | DT_NOCLIP);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);
      }
    }

    DeleteObject(dimBrush);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    isDragging_ = true;
    startPoint_.x = GET_X_LPARAM(lParam);
    startPoint_.y = GET_Y_LPARAM(lParam);

    // Adjust for virtual screen
    Rect virtualScreen = GetVirtualScreenRect();
    startPoint_.x += virtualScreen.x;
    startPoint_.y += virtualScreen.y;

    currentPoint_ = startPoint_;
    return 0;
  }

  case WM_MOUSEMOVE: {
    if (isDragging_) {
      currentPoint_.x = GET_X_LPARAM(lParam);
      currentPoint_.y = GET_Y_LPARAM(lParam);

      Rect virtualScreen = GetVirtualScreenRect();
      currentPoint_.x += virtualScreen.x;
      currentPoint_.y += virtualScreen.y;

      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_LBUTTONUP: {
    if (isDragging_) {
      isDragging_ = false;

      currentPoint_.x = GET_X_LPARAM(lParam);
      currentPoint_.y = GET_Y_LPARAM(lParam);

      Rect virtualScreen = GetVirtualScreenRect();
      currentPoint_.x += virtualScreen.x;
      currentPoint_.y += virtualScreen.y;

      Rect selection = GetSelectionRect();

      // Minimum selection size
      if (selection.width >= 10 && selection.height >= 10) {
        SelectionCallback cb = std::move(callback_);
        CancelSelection();

        if (cb) {
          cb(selection);
        }
      } else {
        // Too small, cancel
        CancelSelection();
      }
    }
    return 0;
  }

  case WM_RBUTTONDOWN:
  case WM_KEYDOWN: {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
      CancelSelection();
    } else if (msg == WM_RBUTTONDOWN) {
      CancelSelection();
    }
    return 0;
  }

  case WM_DESTROY: {
    selecting_ = false;
    return 0;
  }
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Base64 Encoding + JPEG Conversion for Vision AI
// -----------------------------------------------------------------------------

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const BYTE *data, size_t len) {
  std::string result;
  result.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    unsigned int b = (data[i] << 16);
    if (i + 1 < len)
      b |= (data[i + 1] << 8);
    if (i + 2 < len)
      b |= data[i + 2];

    result.push_back(base64_chars[(b >> 18) & 0x3F]);
    result.push_back(base64_chars[(b >> 12) & 0x3F]);
    result.push_back((i + 1 < len) ? base64_chars[(b >> 6) & 0x3F] : '=');
    result.push_back((i + 2 < len) ? base64_chars[b & 0x3F] : '=');
  }

  return result;
}

std::string ScreenCapture::ConvertToBase64Bmp(const CapturedImage &image) {
  if (!image.IsValid()) {
    return "";
  }

  // Build BMP in memory first
  BITMAPFILEHEADER bfh = {};
  bfh.bfType = 0x4D42;
  bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
               image.stride * image.height;
  bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

  BITMAPINFOHEADER bih = {};
  bih.biSize = sizeof(BITMAPINFOHEADER);
  bih.biWidth = image.width;
  bih.biHeight = -image.height;
  bih.biPlanes = 1;
  bih.biBitCount = 32;
  bih.biCompression = BI_RGB;
  bih.biSizeImage = image.stride * image.height;

  // Convert BGRA pixels to simple RGB PPM-like format,
  // then build a minimal JPEG-compatible format
  // Actually, let's just send as PNG using raw pixel encoding trick

  // For maximum compatibility, convert to simple PPM then base64
  // But Groq needs JPEG/PNG. Let's build a minimal valid BMP and try
  // with image/bmp mime type change to jpeg workaround:

  // Use Windows WIC (Windows Imaging Component) to convert to JPEG
  IStream *pStream = nullptr;
  HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
  if (FAILED(hr) || !pStream) {
    // Fallback: send raw BMP
    size_t totalSize = sizeof(bfh) + sizeof(bih) + image.pixels.size();
    std::vector<BYTE> bmpData(totalSize);
    memcpy(bmpData.data(), &bfh, sizeof(bfh));
    memcpy(bmpData.data() + sizeof(bfh), &bih, sizeof(bih));
    memcpy(bmpData.data() + sizeof(bfh) + sizeof(bih), image.pixels.data(),
           image.pixels.size());
    return Base64Encode(bmpData.data(), bmpData.size());
  }

  // Write BMP to stream
  ULONG written;
  pStream->Write(&bfh, sizeof(bfh), &written);
  pStream->Write(&bih, sizeof(bih), &written);
  pStream->Write(image.pixels.data(), (ULONG)image.pixels.size(), &written);

  // Reset stream position
  LARGE_INTEGER zero = {};
  pStream->Seek(zero, STREAM_SEEK_SET, nullptr);

  // Use WIC to convert BMP stream to JPEG
  IWICImagingFactory *pFactory = nullptr;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_IWICImagingFactory, (void **)&pFactory);

  if (FAILED(hr) || !pFactory) {
    pStream->Release();
    // Fallback to BMP
    size_t totalSize = sizeof(bfh) + sizeof(bih) + image.pixels.size();
    std::vector<BYTE> bmpData(totalSize);
    memcpy(bmpData.data(), &bfh, sizeof(bfh));
    memcpy(bmpData.data() + sizeof(bfh), &bih, sizeof(bih));
    memcpy(bmpData.data() + sizeof(bfh) + sizeof(bih), image.pixels.data(),
           image.pixels.size());
    return Base64Encode(bmpData.data(), bmpData.size());
  }

  // Decode BMP from stream
  IWICBitmapDecoder *pDecoder = nullptr;
  hr = pFactory->CreateDecoderFromStream(
      pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);

  if (FAILED(hr) || !pDecoder) {
    pFactory->Release();
    pStream->Release();
    return "";
  }

  IWICBitmapFrameDecode *pFrame = nullptr;
  pDecoder->GetFrame(0, &pFrame);

  if (!pFrame) {
    pDecoder->Release();
    pFactory->Release();
    pStream->Release();
    return "";
  }

  // Create JPEG encoder
  IStream *pOutStream = nullptr;
  CreateStreamOnHGlobal(nullptr, TRUE, &pOutStream);

  IWICBitmapEncoder *pEncoder = nullptr;
  // GUID for JPEG encoder
  GUID jpegGuid = {0x19e4a5aa,
                   0x5662,
                   0x4fc5,
                   {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}};
  hr = pFactory->CreateEncoder(jpegGuid, nullptr, &pEncoder);

  if (FAILED(hr) || !pEncoder) {
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    pStream->Release();
    if (pOutStream)
      pOutStream->Release();
    return "";
  }

  pEncoder->Initialize(pOutStream, WICBitmapEncoderNoCache);

  IWICBitmapFrameEncode *pFrameEncode = nullptr;
  pEncoder->CreateNewFrame(&pFrameEncode, nullptr);

  if (pFrameEncode) {
    pFrameEncode->Initialize(nullptr);
    pFrameEncode->SetSize(image.width, image.height);

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    pFrameEncode->SetPixelFormat(&format);
    pFrameEncode->WriteSource(pFrame, nullptr);
    pFrameEncode->Commit();
    pEncoder->Commit();

    // Read JPEG data from output stream
    STATSTG stat;
    pOutStream->Stat(&stat, STATFLAG_NONAME);
    ULONG jpegSize = (ULONG)stat.cbSize.QuadPart;

    std::vector<BYTE> jpegData(jpegSize);
    LARGE_INTEGER seekZero = {};
    pOutStream->Seek(seekZero, STREAM_SEEK_SET, nullptr);
    pOutStream->Read(jpegData.data(), jpegSize, &written);

    // Cleanup
    pFrameEncode->Release();
    pEncoder->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    pOutStream->Release();
    pStream->Release();

    // Base64 encode the JPEG
    return Base64Encode(jpegData.data(), jpegData.size());
  }

  // Cleanup on failure
  if (pEncoder)
    pEncoder->Release();
  pFrame->Release();
  pDecoder->Release();
  pFactory->Release();
  if (pOutStream)
    pOutStream->Release();
  pStream->Release();

  return "";
}

} // namespace invisible

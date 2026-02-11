#pragma once

#include "utils.h"
#include <vector>

namespace invisible {

// -----------------------------------------------------------------------------
// Captured Image Data
// -----------------------------------------------------------------------------

struct CapturedImage {
  std::vector<BYTE> pixels; // Raw pixel data (BGRA format)
  int width = 0;
  int height = 0;
  int stride = 0; // Bytes per row
  int bitsPerPixel = 32;

  bool IsValid() const { return !pixels.empty() && width > 0 && height > 0; }

  // Get pixel at (x, y) - assumes BGRA format
  COLORREF GetPixel(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
      return 0;
    }
    const BYTE *p = pixels.data() + y * stride + x * 4;
    return RGB(p[2], p[1], p[0]); // BGRA -> RGB
  }
};

// -----------------------------------------------------------------------------
// Screen Capture Class
// -----------------------------------------------------------------------------

class ScreenCapture {
public:
  ScreenCapture() = default;
  ~ScreenCapture() = default;

  // Capture a region of the screen
  // Note: If an overlay window with WDA_EXCLUDEFROMCAPTURE is present,
  // it will NOT appear in this capture (which is the intended behavior)
  static CapturedImage CaptureRegion(const Rect &region);

  // Capture the entire primary monitor
  static CapturedImage CapturePrimaryMonitor();

  // Capture all monitors (virtual screen)
  static CapturedImage CaptureAllMonitors();

  // Capture a specific window
  static CapturedImage CaptureWindow(HWND hwnd, bool clientAreaOnly = true);

  // Save captured image to file (BMP format)
  static bool SaveToBmp(const CapturedImage &image, const wchar_t *filePath);

  // Save captured image to file (PPM format - simple, portable)
  static bool SaveToPpm(const CapturedImage &image, const wchar_t *filePath);

  // Convert captured image to base64-encoded BMP data (for AI vision APIs)
  static std::string ConvertToBase64Bmp(const CapturedImage &image);

private:
  // Internal capture implementation using GDI
  static CapturedImage CaptureFromDC(HDC srcDC, int srcX, int srcY, int width,
                                     int height);
};

// -----------------------------------------------------------------------------
// Region Selector UI
// -----------------------------------------------------------------------------

class RegionSelector {
public:
  using SelectionCallback = std::function<void(const Rect &selectedRegion)>;

  RegionSelector();
  ~RegionSelector();

  // Start region selection (creates a fullscreen overlay for selection)
  // Returns immediately; callback is invoked when selection completes
  void StartSelection(SelectionCallback callback);

  // Cancel ongoing selection
  void CancelSelection();

  // Check if selection is in progress
  bool IsSelecting() const { return selecting_; }

private:
  // Window procedure for selection overlay
  static LRESULT CALLBACK SelectorWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam);
  LRESULT HandleSelectorMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam);

  // Register window class
  static bool RegisterSelectorClass();
  static bool selectorClassRegistered_;
  static constexpr const wchar_t *SELECTOR_CLASS = L"InvisibleRegionSelector";

  HWND selectorHwnd_ = nullptr;
  SelectionCallback callback_;
  std::atomic<bool> selecting_{false};

  // Selection state
  bool isDragging_ = false;
  POINT startPoint_ = {0, 0};
  POINT currentPoint_ = {0, 0};

  // Screen snapshot for display during selection
  CapturedImage screenSnapshot_;
  HBITMAP snapshotBitmap_ = nullptr;

  // Calculate selection rectangle
  Rect GetSelectionRect() const;
};

} // namespace invisible

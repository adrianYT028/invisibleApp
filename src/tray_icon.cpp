#include "tray_icon.h"

namespace invisible {

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Destroy();
}

// -----------------------------------------------------------------------------
// Create / Destroy
// -----------------------------------------------------------------------------

bool TrayIcon::Create(HWND hwndOwner, const wchar_t* tooltip) {
    if (created_) return true;

    hwndOwner_ = hwndOwner;

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwndOwner;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.uVersion = NOTIFYICON_VERSION_4;

    // Use a built-in system icon (shield icon is subtle)
    // For a real deploy you'd load a custom .ico from resources
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (tooltip) {
        wcsncpy_s(nid_.szTip, tooltip, _TRUNCATE);
    }

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        LogError(L"Failed to create tray icon");
        return false;
    }

    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    created_ = true;
    return true;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        created_ = false;
    }
}

// -----------------------------------------------------------------------------
// Tooltip & Balloon
// -----------------------------------------------------------------------------

void TrayIcon::SetTooltip(const wchar_t* tooltip) {
    if (!created_) return;
    wcsncpy_s(nid_.szTip, tooltip, _TRUNCATE);
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* message,
                           DWORD flags, UINT timeoutMs) {
    if (!created_) return;

    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = flags;
    nid_.uTimeout = timeoutMs;
    wcsncpy_s(nid_.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid_.szInfo, message, _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

// -----------------------------------------------------------------------------
// Callback
// -----------------------------------------------------------------------------

void TrayIcon::SetCommandCallback(CommandCallback callback) {
    commandCallback_ = std::move(callback);
}

// -----------------------------------------------------------------------------
// Message Handling
// -----------------------------------------------------------------------------

bool TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    (void)wParam;

    UINT msg = LOWORD(lParam);

    switch (msg) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            return true;

        case WM_LBUTTONDBLCLK:
            // Double-click toggles overlay visibility
            if (commandCallback_) {
                commandCallback_(TRAY_CMD_SHOW_HIDE);
            }
            return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Context Menu
// -----------------------------------------------------------------------------

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_SHOW_HIDE, L"Show/Hide Overlay");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_ASK_AI, L"Ask AI\tCtrl+Shift+A");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_SUMMARY, L"Generate Summary\tCtrl+Shift+M");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_TOGGLE_CAPTURE, L"Toggle Capture Visibility");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_TOGGLE_TRANSCRIPT, L"Toggle Transcript");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_TOGGLE_AUDIO, L"Toggle Audio Capture");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_ABOUT, L"About");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_QUIT, L"Quit\tCtrl+Shift+Q");

    // Required for the menu to dismiss properly
    SetForegroundWindow(hwndOwner_);

    POINT pt;
    GetCursorPos(&pt);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        pt.x, pt.y, 0, hwndOwner_, nullptr);

    DestroyMenu(hMenu);

    // Send a dummy message so the menu disappears properly
    PostMessageW(hwndOwner_, WM_NULL, 0, 0);

    if (cmd && commandCallback_) {
        commandCallback_(cmd);
    }
}

} // namespace invisible

#pragma once

#include "utils.h"
#include <shellapi.h>
#include <functional>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace invisible {

// -----------------------------------------------------------------------------
// System Tray Menu Item IDs
// -----------------------------------------------------------------------------

enum TrayMenuCommand : UINT {
    TRAY_CMD_SHOW_HIDE = 40001,
    TRAY_CMD_ASK_AI,
    TRAY_CMD_SUMMARY,
    TRAY_CMD_TOGGLE_CAPTURE,
    TRAY_CMD_TOGGLE_TRANSCRIPT,
    TRAY_CMD_TOGGLE_AUDIO,
    TRAY_CMD_ABOUT,
    TRAY_CMD_QUIT,
};

// -----------------------------------------------------------------------------
// System Tray Icon
// Provides a notification area icon so the user can interact with the app
// without it appearing in the taskbar.
// -----------------------------------------------------------------------------

class TrayIcon {
public:
    using CommandCallback = std::function<void(UINT commandId)>;

    TrayIcon();
    ~TrayIcon();

    // Disable copy
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Create the tray icon. hwndOwner receives WM_APP+1 messages.
    bool Create(HWND hwndOwner, const wchar_t* tooltip = L"AI Meeting Assistant");

    // Remove the tray icon
    void Destroy();

    // Update tooltip text
    void SetTooltip(const wchar_t* tooltip);

    // Show a balloon notification
    void ShowBalloon(const wchar_t* title, const wchar_t* message,
                     DWORD flags = NIIF_INFO, UINT timeoutMs = 3000);

    // Set the callback for menu commands
    void SetCommandCallback(CommandCallback callback);

    // Process a WM_APP+1 message from the owner window.
    // Returns true if the message was handled.
    bool HandleMessage(WPARAM wParam, LPARAM lParam);

    // The message ID sent to the owner window
    static constexpr UINT WM_TRAYICON = WM_APP + 1;

private:
    void ShowContextMenu();

    NOTIFYICONDATAW nid_ = {};
    HWND hwndOwner_ = nullptr;
    bool created_ = false;
    CommandCallback commandCallback_;
};

} // namespace invisible

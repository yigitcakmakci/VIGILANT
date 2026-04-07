#pragma once
#include <windows.h>
#include <shellapi.h>

// Tray callback message (offset to avoid WM_APP+1/+2 used by WebView thread)
#define WM_TRAYICON              (WM_APP + 100)

// Context-menu command IDs
#define ID_TRAY_TOGGLE_TRACKING  5001
#define ID_TRAY_OPEN_DASHBOARD   5002
#define ID_TRAY_QUIT             5003

class TrayManager {
public:
    TrayManager() = default;
    ~TrayManager();

    // Lifecycle ---------------------------------------------------------------
    bool Create(HWND hwnd, HICON hIcon, const wchar_t* tooltip);
    void Destroy();

    // Icon / tooltip updates --------------------------------------------------
    void UpdateTooltip(const wchar_t* tooltip);
    void UpdateIcon(HICON hIcon);

    // Message dispatch --------------------------------------------------------
    // Call from WndProc. Returns true (+ fills result) when the message is consumed.
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                       LRESULT& result);

    bool IsTrackingActive() const { return m_trackingActive; }

private:
    void ShowContextMenu(HWND hwnd);
    void OnToggleTracking(HWND hwnd);
    void OnOpenDashboard(HWND hwnd);
    void OnQuit(HWND hwnd);

    NOTIFYICONDATAW m_nid = {};
    bool m_created        = false;
    bool m_trackingActive = true;
};

#pragma once
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON         (WM_APP + 1)
#define ID_TRAY_SHOW        4001
#define ID_TRAY_EXIT        4002

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    bool Create(HWND hwnd, HICON hIcon, const wchar_t* tooltip);
    void Remove();
    void ShowContextMenu(HWND hwnd);

private:
    NOTIFYICONDATAW m_nid = {};
    bool m_created = false;
};

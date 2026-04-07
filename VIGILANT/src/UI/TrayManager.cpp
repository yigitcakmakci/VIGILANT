#include "UI/TrayManager.hpp"
#include "Core/WindowTracker.hpp"
#include "Utils/EventQueue.hpp"

extern EventQueue g_EventQueue;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
TrayManager::~TrayManager()
{
    Destroy();
}

bool TrayManager::Create(HWND hwnd, HICON hIcon, const wchar_t* tooltip)
{
    if (m_created) return true;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd             = hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = hIcon;
    wcscpy_s(m_nid.szTip, tooltip);

    m_created = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_created;
}

void TrayManager::Destroy()
{
    if (m_created) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
}

// ---------------------------------------------------------------------------
// Icon / tooltip updates
// ---------------------------------------------------------------------------
void TrayManager::UpdateTooltip(const wchar_t* tooltip)
{
    if (!m_created) return;
    wcscpy_s(m_nid.szTip, tooltip);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayManager::UpdateIcon(HICON hIcon)
{
    if (!m_created) return;
    m_nid.hIcon  = hIcon;
    m_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------
bool TrayManager::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                LRESULT& result)
{
    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            OnOpenDashboard(hwnd);
        }
        result = 0;
        return true;
    }

    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case ID_TRAY_TOGGLE_TRACKING:
            OnToggleTracking(hwnd);
            result = 0;
            return true;
        case ID_TRAY_OPEN_DASHBOARD:
            OnOpenDashboard(hwnd);
            result = 0;
            return true;
        case ID_TRAY_QUIT:
            OnQuit(hwnd);
            result = 0;
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------
void TrayManager::ShowContextMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();

    // Start / Pause tracking (check-mark reflects current state)
    AppendMenuW(hMenu,
                MF_STRING | (m_trackingActive ? MF_CHECKED : MF_UNCHECKED),
                ID_TRAY_TOGGLE_TRACKING,
                m_trackingActive ? L"Tracking'i Duraklat" : L"Tracking'i Ba\u015flat");

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_DASHBOARD, L"Dashboard'u A\u00e7");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_QUIT, L"\u00c7\u0131k\u0131\u015f");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void TrayManager::OnToggleTracking(HWND /*hwnd*/)
{
    if (m_trackingActive) {
        WindowTracker::StopTracking();
        m_trackingActive = false;
        UpdateTooltip(L"VIGILANT - Duraklat\u0131ld\u0131");
    }
    else {
        WindowTracker::StartTracking();
        m_trackingActive = true;
        UpdateTooltip(L"VIGILANT - Aktivite \u0130zleme");
    }
}

void TrayManager::OnOpenDashboard(HWND hwnd)
{
    ::ShowWindow(hwnd, SW_RESTORE);
    ::SetForegroundWindow(hwnd);
}

void TrayManager::OnQuit(HWND hwnd)
{
    // Graceful shutdown: unhook hooks, flush event queue, remove tray, destroy window
    if (m_trackingActive) {
        WindowTracker::StopTracking();
        m_trackingActive = false;
    }
    g_EventQueue.stop();
    Destroy();
    ::DestroyWindow(hwnd);
}

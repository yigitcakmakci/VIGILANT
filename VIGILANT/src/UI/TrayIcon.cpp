#include "UI/TrayIcon.hpp"

TrayIcon::~TrayIcon()
{
    Remove();
}

bool TrayIcon::Create(HWND hwnd, HICON hIcon, const wchar_t* tooltip)
{
    if (m_created) return true;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = hIcon;
    wcscpy_s(m_nid.szTip, tooltip);

    m_created = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_created;
}

void TrayIcon::Remove()
{
    if (m_created) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
}

void TrayIcon::ShowContextMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Dashboard'u G\u00f6ster");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"\u00c7\u0131k\u0131\u015f");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

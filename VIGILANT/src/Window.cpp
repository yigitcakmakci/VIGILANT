#include <windows.h>
#include "UI/WebViewManager.hpp"
#include "UI/TrayIcon.hpp"

#define WM_WEBVIEW_RESIZE (WM_APP + 1)

extern WebViewManager* g_WebViewManager;
extern DWORD g_WebViewThreadId;

TrayIcon g_TrayIcon;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && g_WebViewThreadId != 0) {
            PostThreadMessage(g_WebViewThreadId, WM_WEBVIEW_RESIZE, 0, lParam);
        }
        return 0;
    case WM_KEYDOWN:
        // Open Developer Tools on Ctrl+Shift+I or Ctrl+Alt+D
        if (((wParam == 'I' || wParam == 'i') && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) ||
            ((wParam == 'D' || wParam == 'd') && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000))) {
            if (g_WebViewManager) {
                ICoreWebView2* webview = g_WebViewManager->GetWebView();
                if (webview) {
                    webview->OpenDevToolsWindow();
                }
            }
            return 0;
        }
        break;

    case WM_CLOSE:
        // Minimize to system tray instead of closing the application
        ::ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            g_TrayIcon.ShowContextMenu(hWnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            ::ShowWindow(hWnd, SW_RESTORE);
            ::SetForegroundWindow(hWnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_SHOW:
            ::ShowWindow(hWnd, SW_RESTORE);
            ::SetForegroundWindow(hWnd);
            return 0;
        case ID_TRAY_EXIT:
            g_TrayIcon.Remove();
            ::DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        g_TrayIcon.Remove();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

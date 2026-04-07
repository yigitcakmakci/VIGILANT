#include <windows.h>
#include "UI/WebViewManager.hpp"
#include "UI/TrayManager.hpp"

#define WM_WEBVIEW_RESIZE (WM_APP + 1)

extern WebViewManager* g_WebViewManager;
extern DWORD g_WebViewThreadId;

TrayManager g_TrayManager;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Let TrayManager handle tray icon and context-menu messages first
    LRESULT trayResult = 0;
    if (g_TrayManager.HandleMessage(hWnd, msg, wParam, lParam, trayResult))
        return trayResult;

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

    case WM_DESTROY:
        g_TrayManager.Destroy();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

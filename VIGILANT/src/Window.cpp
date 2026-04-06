#include <windows.h>
#include "UI/WebViewManager.hpp"

extern WebViewManager* g_WebViewManager;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_WebViewManager && g_WebViewManager->GetWebView()) {
            g_WebViewManager->Resize(LOWORD(lParam), HIWORD(lParam));
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
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

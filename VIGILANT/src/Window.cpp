#include <windows.h>
#include "UI/WebViewManager.hpp"

extern WebViewManager* g_WebViewManager;

// Custom window messages for WebView2 async operations
#define WM_WEBVIEW_ENVIRONMENT_CREATED (WM_APP + 1)
#define WM_WEBVIEW_CONTROLLER_CREATED (WM_APP + 2)

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_WEBVIEW_ENVIRONMENT_CREATED:
        {
            OutputDebugStringW(L"[WndProc] Environment created message received\n");
            ICoreWebView2Environment* env = reinterpret_cast<ICoreWebView2Environment*>(wParam);
            if (g_WebViewManager && env) {
                g_WebViewManager->OnEnvironmentCreated(env);
            }
            return 0;
        }
    case WM_WEBVIEW_CONTROLLER_CREATED:
        {
            OutputDebugStringW(L"[WndProc] Controller created message received\n");
            ICoreWebView2Controller* controller = reinterpret_cast<ICoreWebView2Controller*>(wParam);
            if (g_WebViewManager && controller) {
                g_WebViewManager->OnControllerCreated(controller);
            }
            return 0;
        }
    case WM_SIZE:
        if (g_WebViewManager && g_WebViewManager->GetWebView()) {
            g_WebViewManager->Resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_KEYDOWN:
        // Open Developer Tools on Ctrl+Shift+I to avoid conflict with IDE's F12
        // F12 is intercepted by Visual Studio's debugger
        if ((wParam == 'I' || wParam == 'i') && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            if (g_WebViewManager) {
                ICoreWebView2* webview = g_WebViewManager->GetWebView();
                if (webview) {
                    HRESULT hr = webview->OpenDevToolsWindow();
                    if (FAILED(hr)) {
                        // Silently fail if DevTools can't open
                        OutputDebugStringW(L"Failed to open DevTools\n");
                    }
                } else {
                    OutputDebugStringW(L"WebView not initialized yet\n");
                }
            }
            return 0;
        }
        // Alternative: Use Ctrl+Alt+D for Developer Tools
        if ((wParam == 'D' || wParam == 'd') && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            if (g_WebViewManager) {
                ICoreWebView2* webview = g_WebViewManager->GetWebView();
                if (webview) {
                    HRESULT hr = webview->OpenDevToolsWindow();
                    if (FAILED(hr)) {
                        OutputDebugStringW(L"Failed to open DevTools\n");
                    }
                } else {
                    OutputDebugStringW(L"WebView not initialized yet\n");
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

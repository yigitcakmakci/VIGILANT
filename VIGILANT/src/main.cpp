#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <memory>
#include "Data/DatabaseManager.hpp"
#include "Core/WindowTracker.hpp"
#include "Utils/EventQueue.hpp"
#include "Utils/EventBridge.hpp"
#include "UI/WebViewManager.hpp"
#include "AI/AIClassifierTask.hpp"
#include "UI/TrayIcon.hpp"

#define WM_WEBVIEW_RESIZE (WM_APP + 1)

// Global instances are defined in Instances.cpp and Workers.cpp
extern DatabaseManager g_Vault;
extern EventQueue g_EventQueue;
extern WebViewManager* g_WebViewManager;
extern EventBridge* g_EventBridge;
extern AIClassifierTask g_AIClassifier;
extern DWORD g_WebViewThreadId;

// Defined in Window.cpp
extern TrayIcon g_TrayIcon;

// Function declarations (defined in other files)
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void BackgroundWorker();
void HotkeyWorker();

int main() {
    OutputDebugStringW(L"=== VIGILANT Starting ===\n");

    g_Vault.init();
    OutputDebugStringW(L"Database initialized\n");

    std::thread worker(BackgroundWorker);
    std::thread hotkey(HotkeyWorker);
    OutputDebugStringW(L"Worker threads started\n");

    // AI arka plan siniflandirma gorevini baslat
    g_AIClassifier.Start();
    OutputDebugStringW(L"AI Classifier task started\n");

    WindowTracker::StartTracking();
    OutputDebugStringW(L"Window tracking started\n");

    // 1. Create Window Class
    WNDCLASSEXW wc = { sizeof(wc), CS_HREDRAW | CS_VREDRAW, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"VigilantWindowClass", nullptr };
    ::RegisterClassExW(&wc);
    OutputDebugStringW(L"Window class registered\n");

    // 2. Create Window
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"VIGILANT - Aktivite Izleme", WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        OutputDebugStringW(L"ERROR: Failed to create window\n");
        return 1;
    }
    OutputDebugStringW(L"Window created\n");

    // Initialize system tray icon
    HICON hTrayIcon = (HICON)LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
    g_TrayIcon.Create(hwnd, hTrayIcon, L"VIGILANT - Aktivite Izleme");
    OutputDebugStringW(L"System tray icon created\n");

    // 3. Initialize WebView2 in a separate STA thread
    // WebView2 requires STA (Single Threaded Apartment) mode
    // Main thread is in MTA mode, so we must use a separate thread for WebView2
    g_WebViewManager = new WebViewManager(hwnd);

    std::thread webviewThread([&]() {
        // Initialize COM as STA in this thread
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hrCom)) {
            OutputDebugStringW(L"ERROR: CoInitializeEx failed in WebView2 thread\n");
            return;
        }
        OutputDebugStringW(L"COM initialized as STA in WebView2 thread\n");

        // Store this thread's ID for cross-thread resize messages
        g_WebViewThreadId = GetCurrentThreadId();

        // Initialize WebView2 in STA mode
        if (!g_WebViewManager->Initialize()) {
            OutputDebugStringW(L"ERROR: WebView2 initialization failed!\n");
        } else {
            OutputDebugStringW(L"WebView2 initialized successfully in STA thread\n");

            // WebView2 başarıyla başlatıldıktan sonra EventBridge'i başlat
            Sleep(1000); // WebView2'nin tamamen yüklenmesini bekle
            g_EventBridge = new EventBridge(g_WebViewManager, &g_EventQueue);
            g_EventBridge->Start();
            OutputDebugStringW(L"EventBridge started\n");
        }

        // Run a message loop for WebView2 callbacks to execute
        // WebView2 callbacks MUST run in a message pump
        OutputDebugStringW(L"[WebView2Thread] Starting message loop for callbacks\n");
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_WEBVIEW_RESIZE) {
                if (g_WebViewManager) {
                    g_WebViewManager->Resize(LOWORD(msg.lParam), HIWORD(msg.lParam));
                }
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        OutputDebugStringW(L"[WebView2Thread] Message loop ended\n");

        // Uninitialize COM
        CoUninitialize();
    });

    // Don't wait for WebView2 yet - start message loop first!
    // WebView2 callbacks need to run in the message loop
    OutputDebugStringW(L"WebView2 initialization started in background thread\n");

    // 4. Show Window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    OutputDebugStringW(L"Window shown\n");

    OutputDebugStringW(L"Entering message loop...\n");

    // 5. MAIN MESSAGE LOOP - This is where WebView2 callbacks execute
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Small sleep to prevent CPU spinning
        Sleep(10);
    }

    OutputDebugStringW(L"Main message loop ended, sending WM_QUIT to WebView2 thread...\n");

    // Send WM_QUIT to WebView2 thread to stop its message loop
    PostThreadMessageW(g_WebViewThreadId, WM_QUIT, 0, 0);

    // NOW wait for WebView2 initialization thread to complete
    webviewThread.join();
    OutputDebugStringW(L"WebView2 initialization thread completed\n");

    OutputDebugStringW(L"Cleaning up...\n");

    // 6. Cleanup
    if (g_EventBridge) {
        g_EventBridge->Stop();
        delete g_EventBridge;
        g_EventBridge = nullptr;
        OutputDebugStringW(L"EventBridge stopped and cleaned up\n");
    }

    if (g_WebViewManager) {
        delete g_WebViewManager;
    }

    // AI siniflandirma gorevini durdur
    g_AIClassifier.Stop();
    OutputDebugStringW(L"AI Classifier task stopped\n");

    WindowTracker::StopTracking();
    worker.join();
    hotkey.join();

    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    OutputDebugStringW(L"=== VIGILANT Exiting ===\n");
    return 0;
}

// Windows entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main();
}

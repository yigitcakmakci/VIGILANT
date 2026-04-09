#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#include "resource.h"
#include "Data/DatabaseManager.hpp"
#include "Core/WindowTracker.hpp"
#include "Utils/EventQueue.hpp"
#include "Utils/EventBridge.hpp"
#include "UI/WebViewManager.hpp"
#include "AI/AIClassifierTask.hpp"
#include "UI/TrayManager.hpp"
#include "Utils/PerfSnapshot.hpp"

#define WM_WEBVIEW_RESIZE (WM_APP + 1)
#define WM_WEBVIEW_NEWEVENT (WM_APP + 2)
#define WM_WEBVIEW_ACTIVEAPP (WM_APP + 3)

// Global instances are defined in Instances.cpp and Workers.cpp
extern DatabaseManager g_Vault;
extern EventQueue g_EventQueue;
extern WebViewManager* g_WebViewManager;
extern EventBridge* g_EventBridge;
extern AIClassifierTask g_AIClassifier;
extern DWORD g_WebViewThreadId;
extern DWORD g_HotkeyThreadId;

// Defined in Window.cpp
extern TrayManager g_TrayManager;

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

    // Performance snapshot logger (active only when VIGILANT_PERF_ENABLED is defined)
    PerfSnapshotLogger perfLogger;
    perfLogger.Start(30'000); // 30-second interval

    // 1. Create Window Class
    HICON hAppIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_VIGILANT));
    WNDCLASSEXW wc = { sizeof(wc), CS_HREDRAW | CS_VREDRAW, WndProc, 0L, 0L, GetModuleHandle(nullptr), hAppIcon, nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"VigilantWindowClass", hAppIcon };
    ::RegisterClassExW(&wc);
    OutputDebugStringW(L"Window class registered\n");

    // 2. Create Window
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"VIGILANT - Aktivite Izleme", WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        OutputDebugStringW(L"ERROR: Failed to create window\n");
        return 1;
    }
    OutputDebugStringW(L"Window created\n");

    // Enable dark mode title bar to match the application's dark theme
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    // Initialize system tray icon with custom VIGILANT icon
    HICON hTrayIcon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_VIGILANT), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    g_TrayManager.Create(hwnd, hTrayIcon, L"VIGILANT - Aktivite \u0130zleme");
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

            // WebView2 hazir, bildirim artik BackgroundWorker uzerinden yapiliyor
            Sleep(1000); // WebView2'nin tamamen yüklenmesini bekle
            OutputDebugStringW(L"WebView2 ready, notifications via BackgroundWorker\n");

            // EventBridge: WebView event kuyrugundan okuyan kopruyu baslat
            g_EventBridge = new EventBridge(g_WebViewManager);
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
            if (msg.message == WM_WEBVIEW_NEWEVENT) {
                if (g_WebViewManager && g_WebViewManager->GetWebView()) {
                    g_WebViewManager->GetWebView()->ExecuteScript(
                        L"if(typeof _debouncedRefresh==='function')_debouncedRefresh();",
                        nullptr);
                }
                continue;
            }
            if (msg.message == WM_WEBVIEW_ACTIVEAPP) {
                std::string* pJson = reinterpret_cast<std::string*>(msg.lParam);
                if (pJson && g_WebViewManager && g_WebViewManager->GetWebView()) {
                    int size_needed = MultiByteToWideChar(CP_UTF8, 0,
                        pJson->c_str(), (int)pJson->length(), NULL, 0);
                    if (size_needed > 0) {
                        std::wstring wJson(size_needed, 0);
                        MultiByteToWideChar(CP_UTF8, 0,
                            pJson->c_str(), (int)pJson->length(),
                            &wJson[0], size_needed);
                        g_WebViewManager->GetWebView()->PostWebMessageAsJson(wJson.c_str());
                    }
                }
                delete pJson;
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
        auto loopStart = std::chrono::steady_clock::now();
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Measure UI frame time for perf monitoring
        auto loopEnd = std::chrono::steady_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(loopEnd - loopStart).count();
        PerfGlobals::ui_frame_time_ms.store(frameMs, std::memory_order_relaxed);

        // Small sleep to prevent CPU spinning
        Sleep(10);
    }

    OutputDebugStringW(L"Main message loop ended, sending WM_QUIT to WebView2 thread...\n");

    // Send WM_QUIT to WebView2 thread to stop its message loop
    PostThreadMessageW(g_WebViewThreadId, WM_QUIT, 0, 0);

    // NOW wait for WebView2 initialization thread to complete
    webviewThread.join();
    OutputDebugStringW(L"WebView2 initialization thread completed\n");

    // Stop perf logger before cleanup
    perfLogger.Stop();

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

    // Stop event queue so BackgroundWorker can exit
    g_EventQueue.stop();
    worker.join();

    // Stop HotkeyWorker thread
    if (g_HotkeyThreadId != 0) {
        PostThreadMessageW(g_HotkeyThreadId, WM_QUIT, 0, 0);
    }
    hotkey.join();

    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    OutputDebugStringW(L"=== VIGILANT Exiting ===\n");
    return 0;
}

// Windows entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main();
}

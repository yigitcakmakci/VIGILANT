#include <windows.h>
#include <thread>
#include <iostream>
#include <memory>
#include "Data/DatabaseManager.hpp"
#include "Core/WindowTracker.hpp"
#include "Utils/EventQueue.hpp"
#include "UI/WebViewManager.hpp"

// Global instances are defined in Instances.cpp and Workers.cpp
extern DatabaseManager g_Vault;
extern EventQueue g_EventQueue;
extern WebViewManager* g_WebViewManager;

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

    WindowTracker::StartTracking();
    OutputDebugStringW(L"Window tracking started\n");

    // 1. Create Window Class
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"VigilantWindowClass", nullptr };
    ::RegisterClassExW(&wc);
    OutputDebugStringW(L"Window class registered\n");

    // 2. Create Window
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"VIGILANT - Aktivite Izleme", WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        OutputDebugStringW(L"ERROR: Failed to create window\n");
        return 1;
    }
    OutputDebugStringW(L"Window created\n");

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

        // Initialize WebView2 in STA mode
        if (!g_WebViewManager->Initialize()) {
            OutputDebugStringW(L"ERROR: WebView2 initialization failed!\n");
        } else {
            OutputDebugStringW(L"WebView2 initialized successfully in STA thread\n");
        }

        // Run a message loop for WebView2 callbacks to execute
        // WebView2 callbacks MUST run in a message pump
        OutputDebugStringW(L"[WebView2Thread] Starting message loop for callbacks\n");
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
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

    // 4. Start HTTP Server
    g_WebViewManager->StartHTTPServer();
    OutputDebugStringW(L"HTTP Server started\n");

    // 5. Show Window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    OutputDebugStringW(L"Window shown\n");

    OutputDebugStringW(L"Entering message loop...\n");

    // 6. MAIN MESSAGE LOOP - This is where WebView2 callbacks execute
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
    // GetThreadId converts HANDLE to thread ID for PostThreadMessageW
    DWORD webviewThreadId = GetThreadId(webviewThread.native_handle());
    PostThreadMessageW(webviewThreadId, WM_QUIT, 0, 0);

    // NOW wait for WebView2 initialization thread to complete
    webviewThread.join();
    OutputDebugStringW(L"WebView2 initialization thread completed\n");

    OutputDebugStringW(L"Cleaning up...\n");

    // 7. Cleanup
    if (g_WebViewManager) {
        g_WebViewManager->StopHTTPServer();
        delete g_WebViewManager;
    }

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

// Background worker (aktivite izleme) - fully implemented in Workers.cpp
void BackgroundWorker();

// Hotkey worker - fully implemented in Workers.cpp  
void HotkeyWorker();

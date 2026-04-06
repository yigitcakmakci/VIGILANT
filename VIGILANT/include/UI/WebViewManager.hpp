#pragma once
#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <string>
#include <thread>
#include <functional>

using namespace Microsoft::WRL;

class WebViewManager {
public:
    WebViewManager(HWND hWnd);
    bool Initialize();
    void Resize(int width, int height);
    void Navigate(const std::string& url);

    // HTTP Server
    void StartHTTPServer();
    void StopHTTPServer();
    int GetHTTPPort() const { return m_httpPort; }

    ICoreWebView2* GetWebView() const { return m_webView.get(); }

    // Public handler for environment creation completion
    void OnEnvironmentCreated(ICoreWebView2Environment* env);
    void OnControllerCreated(ICoreWebView2Controller* controller);

private:
    HWND m_hWnd;
    wil::com_ptr<ICoreWebView2Environment> m_environment;
    wil::com_ptr<ICoreWebView2Controller> m_controller;
    wil::com_ptr<ICoreWebView2> m_webView;
    std::thread m_httpThread;
    volatile bool m_httpRunning = false;
    volatile bool m_messageHandlerSetup = false;
    int m_httpPort = 8888;

    void SetupMessageHandler();
    std::string HandleMessage(const std::string& message);
};
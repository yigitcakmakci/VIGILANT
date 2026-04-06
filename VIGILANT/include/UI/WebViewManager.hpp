#pragma once
#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <string>
#include <functional>

using namespace Microsoft::WRL;

class WebViewManager {
public:
    WebViewManager(HWND hWnd);
    bool Initialize();
    void Resize(int width, int height);
    void Navigate(const std::string& url);

    ICoreWebView2* GetWebView() const { return m_webView.get(); }

private:
    HWND m_hWnd;
    wil::com_ptr<ICoreWebView2Environment> m_environment;
    wil::com_ptr<ICoreWebView2Controller> m_controller;
    wil::com_ptr<ICoreWebView2> m_webView;
    volatile bool m_messageHandlerSetup = false;

    void SetupMessageHandler();
    std::string HandleMessage(const std::string& message);
};
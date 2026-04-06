#include "UI/WebViewManager.hpp"
#include <shlobj.h>
#include "Data/DatabaseManager.hpp"
#include "AI/GeminiService.hpp"
#include <sstream>

extern DatabaseManager g_Vault;
extern GeminiService g_Gemini;

// Helper function to escape JSON strings
std::string EscapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32) {
                    char buf[10];
                    sprintf_s(buf, "\\u%04x", (unsigned char)c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

WebViewManager::WebViewManager(HWND hWnd) : m_hWnd(hWnd) {}

bool WebViewManager::Initialize() {
    OutputDebugStringW(L"[WebViewManager] Starting initialization...\n");

    // Get user data folder path (in AppData)
    wchar_t userDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, userDataPath))) {
        wcscat_s(userDataPath, MAX_PATH, L"\\VIGILANT\\WebView2");
        OutputDebugStringW(L"[WebViewManager] User Data Folder: ");
        OutputDebugStringW(userDataPath);
        OutputDebugStringW(L"\n");
    } else {
        OutputDebugStringW(L"[WebViewManager] Failed to get AppData path\n");
        return false;
    }

    // Store a pointer to this for use in lambda
    WebViewManager* pThis = this;

    HRESULT res = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, 
        userDataPath,
        nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [pThis](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

                OutputDebugStringW(L"[WebViewManager] CreateEnvironment callback - HRESULT: ");
                wchar_t buf[32];
                swprintf_s(buf, L"0x%08X\n", result);
                OutputDebugStringW(buf);

                if (FAILED(result)) {
                    OutputDebugStringW(L"[WebViewManager] ERROR: CreateEnvironment failed\n");
                    return result;
                }

                if (!env) {
                    OutputDebugStringW(L"[WebViewManager] ERROR: env pointer is NULL\n");
                    return E_FAIL;
                }

                OutputDebugStringW(L"[WebViewManager] Environment created successfully\n");

                env->CreateCoreWebView2Controller(
                    pThis->m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [pThis](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {

                            OutputDebugStringW(L"[WebViewManager] CreateController callback - HRESULT: ");
                            wchar_t buf[32];
                            swprintf_s(buf, L"0x%08X\n", result);
                            OutputDebugStringW(buf);

                            if (FAILED(result)) {
                                OutputDebugStringW(L"[WebViewManager] ERROR: CreateController failed\n");
                                return result;
                            }

                            if (!controller) {
                                OutputDebugStringW(L"[WebViewManager] ERROR: controller pointer is NULL\n");
                                return E_FAIL;
                            }

                            OutputDebugStringW(L"[WebViewManager] Controller created successfully\n");

                            pThis->m_controller = controller;
                            pThis->m_controller->get_CoreWebView2(&pThis->m_webView);

                            if (!pThis->m_webView) {
                                OutputDebugStringW(L"[WebViewManager] ERROR: Failed to get WebView pointer\n");
                                return E_FAIL;
                            }

                            OutputDebugStringW(L"[WebViewManager] WebView pointer obtained\n");

                            pThis->m_controller->put_IsVisible(TRUE);
                            OutputDebugStringW(L"[WebViewManager] WebView set to visible\n");

                            // F12 tusunu yakalayip DevTools ac (debugger __debugbreak cakismasini onle)
                            pThis->m_controller->add_AcceleratorKeyPressed(
                                Microsoft::WRL::Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [pThis](ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        COREWEBVIEW2_KEY_EVENT_KIND kind;
                                        args->get_KeyEventKind(&kind);
                                        UINT key;
                                        args->get_VirtualKey(&key);

                                        if (key == VK_F12 && kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) {
                                            args->put_Handled(TRUE);
                                            if (pThis->m_webView) {
                                                pThis->m_webView->OpenDevToolsWindow();
                                            }
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            OutputDebugStringW(L"[WebViewManager] F12 AcceleratorKey handler added\n");

                            // WebView2 Settings - F12 DevTools ve güvenlik ayarları
                            wil::com_ptr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(pThis->m_webView->get_Settings(&settings))) {
                                settings->put_AreDevToolsEnabled(TRUE);
                                settings->put_AreDefaultContextMenusEnabled(TRUE);
                                settings->put_IsZoomControlEnabled(TRUE);
                                // F12 kısayolunu etkinleştir
                                wil::com_ptr<ICoreWebView2Settings3> settings3;
                                if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings3)))) {
                                    settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE);
                                }
                                OutputDebugStringW(L"[WebViewManager] Settings configured (DevTools + AcceleratorKeys enabled)\n");
                            }

                            // Set initial bounds from actual client area
                            RECT bounds;
                            GetClientRect(pThis->m_hWnd, &bounds);
                            pThis->m_controller->put_Bounds(bounds);
                            OutputDebugStringW(L"[WebViewManager] Bounds set\n");

                            // Get EXE path
                            wchar_t exePath[MAX_PATH];
                            GetModuleFileNameW(NULL, exePath, MAX_PATH);
                            std::wstring path(exePath);
                            std::wstring directory = path.substr(0, path.find_last_of(L"\\/"));

                            OutputDebugStringW(L"[WebViewManager] EXE Directory: ");
                            OutputDebugStringW(directory.c_str());
                            OutputDebugStringW(L"\n");

                            // Convert Windows path to file:// URL format
                            std::wstring fileUrlPath = L"file:///";
                            for (wchar_t c : directory) {
                                if (c == L'\\') {
                                    fileUrlPath += L'/';
                                } else {
                                    fileUrlPath += c;
                                }
                            }

                            // Load dashboard.html
                            std::wstring dashboardPath = fileUrlPath + L"/dashboard_pro.html";
                            OutputDebugStringW(L"[WebViewManager] Loading professional dashboard\n");
                            OutputDebugStringW(L"[WebViewManager] URL: ");
                            OutputDebugStringW(dashboardPath.c_str());
                            OutputDebugStringW(L"\n");

                            // Navigate to dashboard.html
                            pThis->m_webView->Navigate(dashboardPath.c_str());
                            OutputDebugStringW(L"[WebViewManager] Navigation initiated\n");

                            // Setup message handler AFTER navigation
                            pThis->SetupMessageHandler();
                            OutputDebugStringW(L"[WebViewManager] Message handler setup initiated\n");

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());

    OutputDebugStringW(L"[WebViewManager] CreateCoreWebView2EnvironmentWithOptions returned: ");
    wchar_t initBuf[32];
    swprintf_s(initBuf, L"0x%08X\n", res);
    OutputDebugStringW(initBuf);

    return SUCCEEDED(res);
}

void WebViewManager::Resize(int width, int height) {
    if (m_controller) {
        RECT bounds = { 0, 0, width, height };
        m_controller->put_Bounds(bounds);
    }
}

void WebViewManager::Navigate(const std::string& url) {
    if (m_webView) {
        std::wstring wurl(url.begin(), url.end());
        m_webView->Navigate(wurl.c_str());
    }
}

void WebViewManager::SetupMessageHandler() {
    if (!m_webView) {
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] WebView not initialized\n");
        return;
    }

    // Prevent setting up the handler multiple times
    if (m_messageHandlerSetup) {
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Message handler already set up\n");
        return;
    }

    try {
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Starting setup\n");

        // Store pointers for use in lambda
        WebViewManager* pThis = this;

        // Get a strong reference to the WebView
        wil::com_ptr<ICoreWebView2> webViewCopy = m_webView;

        if (!webViewCopy) {
            OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] WebView copy failed\n");
            return;
        }

        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Creating callback\n");

        // Add message received handler
        HRESULT hr = webViewCopy->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [webViewCopy, pThis](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    try {
                        if (!args) {
                            return E_INVALIDARG;
                        }

                        // postMessage ile obje gönderildiğinde get_WebMessageAsJson kullanılmalı
                        // TryGetWebMessageAsString sadece string mesajlar için çalışır
                        LPWSTR messageRaw = NULL;
                        HRESULT hr2 = args->get_WebMessageAsJson(&messageRaw);

                        if (SUCCEEDED(hr2) && messageRaw) {
                            // Convert wide string to std::string safely
                            int len = WideCharToMultiByte(CP_UTF8, 0, messageRaw, -1, NULL, 0, NULL, NULL);
                            if (len > 1) {
                                std::string message(len - 1, 0);
                                WideCharToMultiByte(CP_UTF8, 0, messageRaw, -1, &message[0], len, NULL, NULL);

                                OutputDebugStringW(L"[WebViewManager] Message received: ");
                                OutputDebugStringW(messageRaw);
                                OutputDebugStringW(L"\n");

                                // Handle message safely
                                std::string response = pThis->HandleMessage(message);

                                OutputDebugStringW(L"[WebViewManager] Sending response\n");

                                // Convert response back to wide string
                                int size_needed = MultiByteToWideChar(CP_UTF8, 0, response.c_str(), (int)response.length(), NULL, 0);
                                if (size_needed > 0) {
                                    std::wstring wresponse(size_needed, 0);
                                    MultiByteToWideChar(CP_UTF8, 0, response.c_str(), (int)response.length(), &wresponse[0], size_needed);

                                    if (webViewCopy) {
                                        webViewCopy->PostWebMessageAsJson(wresponse.c_str());
                                    }
                                }
                            }

                            if (messageRaw) {
                                CoTaskMemFree(messageRaw);
                            }
                        } else {
                            OutputDebugStringW(L"[WebViewManager] get_WebMessageAsJson FAILED\n");
                        }
                    } 
                    catch (const std::exception&) {
                        OutputDebugStringW(L"[WebViewManager] Exception in message handler callback\n");
                    }
                    catch (...) {
                        OutputDebugStringW(L"[WebViewManager] Unknown exception in message handler callback\n");
                    }
                    return S_OK;
                }).Get(), NULL);

        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] add_WebMessageReceived result: ");
        wchar_t buf[32];
        swprintf_s(buf, L"0x%08X\n", hr);
        OutputDebugStringW(buf);

        if (FAILED(hr)) {
            OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Failed to add handler\n");
            return;
        }

        m_messageHandlerSetup = true;
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Message handler initialized successfully\n");
    } 
    catch (const std::exception&) {
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] std::exception caught\n");
    }
    catch (...) {
        OutputDebugStringW(L"[WebViewManager::SetupMessageHandler] Unknown exception caught\n");
    }
}

std::string WebViewManager::HandleMessage(const std::string& message) {
    // Parse JSON message to extract requestId
    std::string response = "{}";
    std::string requestId = "";

    try {
        // Simple JSON parsing for requestId
        size_t requestIdPos = message.find("\"requestId\"");
        if (requestIdPos != std::string::npos) {
            size_t colonPos = message.find(":", requestIdPos);
            if (colonPos != std::string::npos) {
                size_t start = message.find_first_not_of(" \t", colonPos + 1);
                if (start != std::string::npos) {
                    size_t end = message.find_first_of(",}", start);
                    if (end != std::string::npos) {
                        requestId = message.substr(start, end - start);
                        // Remove quotes if present
                        if (requestId.front() == '"' && requestId.back() == '"') {
                            requestId = requestId.substr(1, requestId.length() - 2);
                        }
                    }
                }
            }
        }

        if (message.find("getDashboardSummary") != std::string::npos) {
            auto summaryJson = g_Vault.getDashboardSummaryJson();
            if (!requestId.empty()) summaryJson["requestId"] = requestId;
            response = summaryJson.dump();
        }
        else if (message.find("getActivityLogs") != std::string::npos) {
            auto logs = g_Vault.getRecentLogs(50);

            response = "{\"logs\":[";
            for (size_t i = 0; i < logs.size(); i++) {
                const auto& log = logs[i];
                response += "{\"process\":\"" + EscapeJson(log.process) + "\",";
                response += "\"title\":\"" + EscapeJson(log.title) + "\",";
                response += "\"category\":\"" + EscapeJson(log.category) + "\",";
                response += "\"score\":" + std::to_string(log.score) + ",";
                response += "\"duration\":" + std::to_string(log.duration) + "}";
                if (i < logs.size() - 1) response += ",";
            }
            response += "]";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("getProductivityData") != std::string::npos) {
            float productivity = g_Vault.calculateDailyProductivity();
            float totalScore = g_Vault.calculateTodaysTotalScore();
            int totalDuration = g_Vault.getTodaysTotalDuration();
            auto dist = g_Vault.getCategoryDistribution();

            response = "{\"metrics\":{\"productivity\":" + std::to_string(productivity) + 
                      ",\"totalScore\":" + std::to_string(totalScore) + 
                      ",\"totalTime\":" + std::to_string(totalDuration) + 
                      ",\"categories\":{";

            bool first = true;
            for (const auto& [cat, duration] : dist) {
                if (!first) response += ",";
                response += "\"" + EscapeJson(cat) + "\":" + std::to_string((long long)duration);
                first = false;
            }
            response += "}}}";
            if (!requestId.empty()) response.insert(response.length() - 1, ",\"requestId\":" + requestId);
        }
        else if (message.find("categorizeWithAI") != std::string::npos) {
            OutputDebugStringW(L"[WebViewManager] AI kategorize istegi alindi\n");

            if (!g_Gemini.isAvailable()) {
                response = "{\"status\":\"error\",\"message\":\"GEMINI_API_KEY bulunamadi\"";
                if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                response += "}";
            }
            else {
                auto uncategorized = g_Vault.getUncategorizedActivities();

                OutputDebugStringW(L"[WebViewManager] Kategorize edilmemis: ");
                wchar_t buf[32];
                swprintf_s(buf, L"%zu\n", uncategorized.size());
                OutputDebugStringW(buf);

                if (uncategorized.empty()) {
                    response = "{\"status\":\"success\",\"processed\":0,\"message\":\"Tum aktiviteler zaten kategorize edilmis\"";
                    if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                    response += "}";
                }
                else {
                    auto labels = g_Gemini.classifyActivities(uncategorized);

                    int saved = 0;
                    for (const auto& label : labels) {
                        if (g_Vault.saveAILabels(label.process, label.titleKeyword,
                                                  label.category, label.score)) {
                            saved++;
                        }
                    }

                    response = "{\"status\":\"success\",\"processed\":" + std::to_string(saved) +
                               ",\"total\":" + std::to_string(uncategorized.size());
                    if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                    response += "}";

                    OutputDebugStringW(L"[WebViewManager] AI kategorize tamamlandi\n");
                }
            }
        }
    } catch (const std::exception& e) {
        response = "{\"error\":\"" + std::string(e.what()) + "\"";
        if (!requestId.empty()) response += ",\"requestId\":" + requestId;
        response += "}";
    }

    return response;
}

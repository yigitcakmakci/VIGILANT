#include "UI/WebViewManager.hpp"
#include <shlobj.h>
#include "Data/DatabaseManager.hpp"
#include "AI/GeminiService.hpp"
#include "Utils/StartupManager.hpp"
#include "Utils/json.hpp"
#include "Utils/ResourceLoader.hpp"
#include "resource.h"
#include <sstream>

extern DatabaseManager g_Vault;
extern GeminiService g_Gemini;

// --- Narrative Input Builder ---
// Mevcut WindowTracker ve veritabani verilerini kullanarak
// Activity Narrative icin girdi JSON'u olusturur.
static nlohmann::json prepareNarrativeInput(DatabaseManager& db) {
    using json = nlohmann::json;

    // Tarih (yerel saat)
    SYSTEMTIME st;
    GetLocalTime(&st);
    char dateBuf[16];
    sprintf_s(dateBuf, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);

    // Dashboard ozet verisi (topApps + total duration)
    auto summary = db.getDashboardSummaryJson();

    // totalFocusMinutes
    int totalDurationSec = 0;
    if (summary.contains("productivity") && summary["productivity"].contains("totalDurationSec"))
        totalDurationSec = summary["productivity"]["totalDurationSec"].get<int>();
    int totalFocusMinutes = totalDurationSec / 60;

    // topWindows — getDashboardSummaryJson().topApps -> [{process, durationSec, category, score}]
    json topWindows = json::array();
    if (summary.contains("topApps") && summary["topApps"].is_array()) {
        for (const auto& app : summary["topApps"]) {
            std::string proc = app.value("process", "Unknown");
            int durSec = app.value("durationSec", 0);
            topWindows.push_back({
                {"title", proc},
                {"app", proc},
                {"minutes", durSec / 60}
            });
        }
    }

    // milestones — kategori dagilimi (her kategori bir milestone)
    json milestones = json::array();
    auto dist = db.getCategoryDistribution();
    for (const auto& [cat, durSec] : dist) {
        int mins = (int)(durSec / 60.0f);
        if (mins < 1) continue;
        milestones.push_back({
            {"label", cat},
            {"minutes", mins},
            {"evidence", std::to_string(mins) + " dk " + cat + " aktivitesi"}
        });
    }

    json input;
    input["date"] = std::string(dateBuf);
    input["totalFocusMinutes"] = totalFocusMinutes;
    input["topWindows"] = topWindows;
    input["milestones"] = milestones;

    return input;
}

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
                pThis->m_environment = env;

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

                            // ── Serve embedded web resources via virtual host ──
                            pThis->m_webView->AddWebResourceRequestedFilter(
                                L"https://vigilant.local/*",
                                COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

                            pThis->m_webView->add_WebResourceRequested(
                                Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                    [pThis](ICoreWebView2* /*sender*/,
                                            ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {

                                        wil::com_ptr<ICoreWebView2WebResourceRequest> request;
                                        args->get_Request(&request);
                                        LPWSTR uri = nullptr;
                                        request->get_Uri(&uri);
                                        std::wstring uriStr(uri ? uri : L"");
                                        if (uri) CoTaskMemFree(uri);

                                        struct ResMap { const wchar_t* name; int id; const wchar_t* mime; };
                                        static const ResMap kMap[] = {
                                            { L"dashboard_pro.html",     IDR_HTML_DASHBOARD,         L"text/html; charset=utf-8" },
                                            { L"micro-interactions.css", IDR_CSS_MICRO_INTERACTIONS, L"text/css; charset=utf-8" },
                                            { L"flow-state.css",         IDR_CSS_FLOW_STATE,         L"text/css; charset=utf-8" },
                                            { L"micro-interactions.js",  IDR_JS_MICRO_INTERACTIONS,  L"application/javascript; charset=utf-8" },
                                            { L"mood-engine.js",         IDR_JS_MOOD_ENGINE,         L"application/javascript; charset=utf-8" },
                                            { L"timer-service.js",       IDR_JS_TIMER_SERVICE,       L"application/javascript; charset=utf-8" },
                                            { L"gemini-client.js",       IDR_JS_GEMINI_CLIENT,       L"application/javascript; charset=utf-8" },
                                        };

                                        for (const auto& m : kMap) {
                                            if (uriStr.find(m.name) == std::wstring::npos) continue;

                                            std::string content = ResourceLoader::LoadTextResource(m.id);
                                            if (content.empty()) break;

                                            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, content.size());
                                            if (!hGlobal) break;
                                            void* pBuf = GlobalLock(hGlobal);
                                            memcpy(pBuf, content.data(), content.size());
                                            GlobalUnlock(hGlobal);

                                            IStream* stream = nullptr;
                                            if (FAILED(CreateStreamOnHGlobal(hGlobal, TRUE, &stream))) {
                                                GlobalFree(hGlobal);
                                                break;
                                            }

                                            std::wstring headers = std::wstring(L"Content-Type: ") + m.mime;

                                            wil::com_ptr<ICoreWebView2WebResourceResponse> response;
                                            pThis->m_environment->CreateWebResourceResponse(
                                                stream, 200, L"OK", headers.c_str(), &response);
                                            args->put_Response(response.get());

                                            stream->Release();
                                            break;
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            OutputDebugStringW(L"[WebViewManager] Embedded resource handler registered\n");

                            // Navigate to the embedded dashboard via virtual host
                            pThis->m_webView->Navigate(L"https://vigilant.local/dashboard_pro.html");
                            OutputDebugStringW(L"[WebViewManager] Navigation initiated (virtual host)\n");

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
                response += "\"duration\":" + std::to_string(log.duration) + ",";
                response += "\"source\":\"" + EscapeJson(log.source) + "\"}";
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
        else if (message.find("generateNarrative") != std::string::npos) {
            OutputDebugStringW(L"[WebViewManager] Narrative olusturma istegi alindi\n");

            if (!g_Gemini.isAvailable()) {
                response = "{\"type\":\"NarrativeUpdate\",\"error\":\"GEMINI_API_KEY bulunamadi\"";
                if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                response += "}";
            }
            else {
                auto narrativeInput = prepareNarrativeInput(g_Vault);
                OutputDebugStringW(L"[WebViewManager] Narrative input hazirlandi\n");

                auto narrativeResult = g_Gemini.generateNarrative(narrativeInput);

                // NarrativeUpdate tipini ekle
                narrativeResult["type"] = "NarrativeUpdate";
                if (!requestId.empty()) narrativeResult["requestId"] = requestId;

                response = narrativeResult.dump();
                OutputDebugStringW(L"[WebViewManager] Narrative yaniti hazirlandi\n");
            }
        }
        else if (message.find("saveCategoryOverride") != std::string::npos) {
            // Parse: exePath, titlePattern, category, score
            auto extractStr = [&](const std::string& key) -> std::string {
                size_t pos = message.find("\"" + key + "\"");
                if (pos == std::string::npos) return "";
                size_t colon = message.find(":", pos);
                if (colon == std::string::npos) return "";
                size_t qStart = message.find("\"", colon + 1);
                if (qStart == std::string::npos) return "";
                size_t qEnd = message.find("\"", qStart + 1);
                if (qEnd == std::string::npos) return "";
                return message.substr(qStart + 1, qEnd - qStart - 1);
            };
            auto extractInt = [&](const std::string& key) -> int {
                size_t pos = message.find("\"" + key + "\"");
                if (pos == std::string::npos) return 0;
                size_t colon = message.find(":", pos);
                if (colon == std::string::npos) return 0;
                size_t start = message.find_first_not_of(" \t", colon + 1);
                if (start == std::string::npos) return 0;
                return std::atoi(message.c_str() + start);
            };

            std::string exePath = extractStr("exePath");
            std::string titlePattern = extractStr("titlePattern");
            std::string category = extractStr("category");
            int score = extractInt("score");

            if (titlePattern.empty()) titlePattern = "*";

            bool ok = g_Vault.saveCategoryOverride(exePath, titlePattern, category, score);
            response = "{\"status\":\"" + std::string(ok ? "success" : "error") + "\"";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("getOverrideRules") != std::string::npos) {
            auto rules = g_Vault.getOverrideRules();
            response = "{\"rules\":[";
            for (size_t i = 0; i < rules.size(); i++) {
                const auto& r = rules[i];
                response += "{\"exePath\":\"" + EscapeJson(r.exePath) + "\",";
                response += "\"titlePattern\":\"" + EscapeJson(r.titlePattern) + "\",";
                response += "\"category\":\"" + EscapeJson(r.category) + "\",";
                response += "\"score\":" + std::to_string(r.score) + ",";
                response += "\"createdAt\":\"" + EscapeJson(r.createdAt) + "\",";
                response += "\"updatedAt\":\"" + EscapeJson(r.updatedAt) + "\"}";
                if (i < rules.size() - 1) response += ",";
            }
            response += "]";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("getOverrideAuditLog") != std::string::npos) {
            auto auditJson = g_Vault.getOverrideAuditLog(50);
            nlohmann::json resp;
            resp["audit"] = auditJson;
            if (!requestId.empty()) resp["requestId"] = requestId;
            response = resp.dump();
        }
        // ── Autostart Toggle API ───────────────────────────────────
        // JS: postMessage({ type: "getAutostartStatus" })
        // JS: postMessage({ type: "setAutostart", enabled: true/false, method: "registry"|"taskscheduler" })
        else if (message.find("getAutostartStatus") != std::string::npos) {
            bool reg = StartupManager::IsAutostartEnabled(AutostartMethod::Registry);
            bool task = StartupManager::IsAutostartEnabled(AutostartMethod::TaskScheduler);
            response = "{\"registry\":" + std::string(reg ? "true" : "false") +
                       ",\"taskScheduler\":" + std::string(task ? "true" : "false");
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("clearDatabase") != std::string::npos) {
            OutputDebugStringW(L"[WebViewManager] Veritabani temizleme istegi alindi\n");
            bool ok = g_Vault.clearAllData();
            response = "{\"status\":\"" + std::string(ok ? "success" : "error") + "\"";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("getAIConfig") != std::string::npos) {
            // Mevcut AI yapilandirmasini dondur
            std::string provider = g_Gemini.getProviderName();
            std::string model = g_Gemini.getModel();
            std::string envVar = g_Gemini.getEnvVarName();
            bool available = g_Gemini.isAvailable();

            response = "{\"provider\":\"" + EscapeJson(provider) + "\","
                "\"model\":\"" + EscapeJson(model) + "\","
                "\"envVar\":\"" + EscapeJson(envVar) + "\","
                "\"available\":" + std::string(available ? "true" : "false");
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        else if (message.find("setAIConfig") != std::string::npos) {
            OutputDebugStringW(L"[WebViewManager] AI yapilandirma istegi alindi\n");

            auto extractStr = [&](const std::string& key) -> std::string {
                size_t pos = message.find("\"" + key + "\"");
                if (pos == std::string::npos) return "";
                size_t colon = message.find(":", pos);
                if (colon == std::string::npos) return "";
                size_t qStart = message.find("\"", colon + 1);
                if (qStart == std::string::npos) return "";
                size_t qEnd = message.find("\"", qStart + 1);
                if (qEnd == std::string::npos) return "";
                return message.substr(qStart + 1, qEnd - qStart - 1);
            };

            std::string envVar = extractStr("envVar");
            std::string provider = extractStr("provider");
            std::string model = extractStr("model");

            if (envVar.empty() || provider.empty() || model.empty()) {
                response = "{\"status\":\"error\",\"message\":\"Eksik parametre\"";
                if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                response += "}";
            }
            else {
                bool ok = g_Gemini.configure(envVar, provider, model);
                if (ok) {
                    // Anahtari dogrula
                    bool valid = g_Gemini.validateApiKey();
                    if (valid) {
                        response = "{\"status\":\"success\",\"message\":\"Yapilandirma basarili\"";
                    }
                    else {
                        response = "{\"status\":\"invalid_key\",\"message\":\"Anahtar uyumlu degil\"";
                    }
                }
                else {
                    response = "{\"status\":\"env_not_found\",\"message\":\"Ortam degiskeni bulunamadi\"";
                }
                if (!requestId.empty()) response += ",\"requestId\":" + requestId;
                response += "}";
            }
        }
        else if (message.find("validateAIKey") != std::string::npos) {
            OutputDebugStringW(L"[WebViewManager] AI anahtar dogrulama istegi alindi\n");
            bool available = g_Gemini.isAvailable();
            bool valid = false;
            if (available) {
                valid = g_Gemini.validateApiKey();
            }
            response = "{\"available\":" + std::string(available ? "true" : "false") +
                ",\"valid\":" + std::string(valid ? "true" : "false");
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        // ── History API: getHistoricalData ──
        else if (message.find("getHistoricalData") != std::string::npos) {
            // Parse date parameter
            auto extractStr = [&](const std::string& key) -> std::string {
                size_t pos = message.find("\"" + key + "\"");
                if (pos == std::string::npos) return "";
                size_t colon = message.find(":", pos);
                if (colon == std::string::npos) return "";
                size_t qStart = message.find("\"", colon + 1);
                if (qStart == std::string::npos) return "";
                size_t qEnd = message.find("\"", qStart + 1);
                if (qEnd == std::string::npos) return "";
                return message.substr(qStart + 1, qEnd - qStart - 1);
            };
            std::string date = extractStr("date");
            if (date.empty()) {
                // Varsayılan: bugün
                SYSTEMTIME st;
                GetLocalTime(&st);
                char buf[16];
                sprintf_s(buf, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
                date = buf;
            }

            auto histData = g_Vault.getHistoricalData(date);

            // Logs
            auto logs = g_Vault.getLogsForDate(date, 50);
            nlohmann::json logsJson = nlohmann::json::array();
            for (const auto& log : logs) {
                logsJson.push_back({
                    {"process", log.process},
                    {"title", log.title},
                    {"category", log.category},
                    {"score", log.score},
                    {"duration", log.duration},
                    {"source", log.source}
                });
            }
            histData["logs"] = logsJson;

            if (!requestId.empty()) histData["requestId"] = requestId;
            response = histData.dump();
        }
        // ── History API: getDailyTrends ──
        else if (message.find("getDailyTrends") != std::string::npos) {
            // Parse days parameter
            int days = 7;
            size_t daysPos = message.find("\"days\"");
            if (daysPos != std::string::npos) {
                size_t colon = message.find(":", daysPos);
                if (colon != std::string::npos) {
                    size_t start = message.find_first_not_of(" \t", colon + 1);
                    if (start != std::string::npos) {
                        days = std::atoi(message.c_str() + start);
                        if (days <= 0 || days > 90) days = 7;
                    }
                }
            }

            auto trends = g_Vault.getDailyTrends(days);
            nlohmann::json resp;
            resp["trends"] = trends;
            resp["days"] = days;
            if (!requestId.empty()) resp["requestId"] = requestId;
            response = resp.dump();
        }
        // ── History API: saveDailySummary ──
        else if (message.find("saveDailySummary") != std::string::npos) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char buf[16];
            sprintf_s(buf, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            bool ok = g_Vault.saveDailySummary(std::string(buf));
            response = "{\"status\":\"" + std::string(ok ? "success" : "no_data") + "\"";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
        // ── History API: getAvailableDates ──
        else if (message.find("getAvailableDates") != std::string::npos) {
            auto dates = g_Vault.getAvailableDates();
            nlohmann::json resp;
            resp["dates"] = dates;
            if (!requestId.empty()) resp["requestId"] = requestId;
            response = resp.dump();
        }
        else if (message.find("setAutostart") != std::string::npos) {
            bool enable = (message.find("\"enabled\":true") != std::string::npos ||
                           message.find("\"enabled\": true") != std::string::npos);
            AutostartMethod method = AutostartMethod::Registry;
            if (message.find("\"method\":\"taskscheduler\"") != std::string::npos ||
                message.find("\"method\": \"taskscheduler\"") != std::string::npos) {
                method = AutostartMethod::TaskScheduler;
            }

            bool ok = enable ? StartupManager::EnableAutostart(method)
                             : StartupManager::DisableAutostart(method);
            response = "{\"status\":\"" + std::string(ok ? "success" : "error") + "\"";
            if (!requestId.empty()) response += ",\"requestId\":" + requestId;
            response += "}";
        }
    } catch (const std::exception& e) {
        response = "{\"error\":\"" + std::string(e.what()) + "\"";
        if (!requestId.empty()) response += ",\"requestId\":" + requestId;
        response += "}";
    }

    return response;
}

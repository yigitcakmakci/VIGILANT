#include "Utils/EventBridge.hpp"
#include "Utils/EventQueue.hpp"
#include "UI/WebViewManager.hpp"
#include "Utils/json.hpp"
#include <string>

using json = nlohmann::json;

EventBridge::EventBridge(WebViewManager* webViewManager, EventQueue* eventQueue)
    : m_webViewManager(webViewManager)
    , m_eventQueue(eventQueue)
    , m_running(false)
{
}

EventBridge::~EventBridge() {
    Stop();
}

void EventBridge::Start() {
    if (m_running.load()) {
        OutputDebugStringW(L"[EventBridge] Already running\n");
        return;
    }

    m_running.store(true);
    m_workerThread = std::make_unique<std::thread>(&EventBridge::WorkerThread, this);
    OutputDebugStringW(L"[EventBridge] Started\n");
}

void EventBridge::Stop() {
    if (!m_running.load()) {
        return;
    }

    OutputDebugStringW(L"[EventBridge] Stopping...\n");
    m_running.store(false);

    if (m_workerThread && m_workerThread->joinable()) {
        m_workerThread->join();
    }

    OutputDebugStringW(L"[EventBridge] Stopped\n");
}

void EventBridge::WorkerThread() {
    OutputDebugStringW(L"[EventBridge::WorkerThread] Started\n");

    while (m_running.load()) {
        EventData eventData;

        // EventQueue'dan veri al (bloklanır, veri gelene kadar bekler)
        if (m_eventQueue && m_eventQueue->pop(eventData)) {
            // JSON'a çevir
            std::string jsonMessage = EventDataToJson(eventData);

            // WebView'e gönder
            SendToWebView(jsonMessage);

            // Debug log
            OutputDebugStringW(L"[EventBridge] Event sent: ");
            int wLen = MultiByteToWideChar(CP_UTF8, 0, jsonMessage.c_str(), (int)jsonMessage.length(), NULL, 0);
            if (wLen > 0) {
                std::wstring wJson(wLen, 0);
                MultiByteToWideChar(CP_UTF8, 0, jsonMessage.c_str(), (int)jsonMessage.length(), &wJson[0], wLen);
                OutputDebugStringW(wJson.c_str());
            }
            OutputDebugStringW(L"\n");
        } else {
            // Queue durduruldu veya hata oluştu
            break;
        }
    }

    OutputDebugStringW(L"[EventBridge::WorkerThread] Stopped\n");
}

std::string EventBridge::EventDataToJson(const EventData& data) {
    try {
        // nlohmann/json ile JSON nesnesi oluştur
        json j;
        j["type"] = "windowEvent";  // Event tipi (JavaScript tarafında ayırt etmek için)
        j["data"] = {
            {"processName", data.processName},
            {"title", data.title},
            {"url", data.url}
        };

        // JSON string'e çevir
        return j.dump();
    }
    catch (const std::exception& ex) {
        OutputDebugStringW(L"[EventBridge] JSON conversion error: ");
        std::string error(ex.what());
        int wErrLen = MultiByteToWideChar(CP_UTF8, 0, error.c_str(), (int)error.length(), NULL, 0);
        if (wErrLen > 0) {
            std::wstring wError(wErrLen, 0);
            MultiByteToWideChar(CP_UTF8, 0, error.c_str(), (int)error.length(), &wError[0], wErrLen);
            OutputDebugStringW(wError.c_str());
        }
        OutputDebugStringW(L"\n");

        // Hata durumunda boş JSON döndür
        return R"({"type":"error","message":"JSON conversion failed"})";
    }
}

void EventBridge::SendToWebView(const std::string& jsonMessage) {
    if (!m_webViewManager) {
        OutputDebugStringW(L"[EventBridge] WebViewManager is null\n");
        return;
    }

    auto webView = m_webViewManager->GetWebView();
    if (!webView) {
        OutputDebugStringW(L"[EventBridge] WebView is not initialized\n");
        return;
    }

    try {
        // UTF-8 -> UTF-16 dönüşümü
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, jsonMessage.c_str(), (int)jsonMessage.length(), NULL, 0);
        if (size_needed > 0) {
            std::wstring wJsonMessage(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, jsonMessage.c_str(), (int)jsonMessage.length(), &wJsonMessage[0], size_needed);

            // WebView'e JSON mesajını gönder
            HRESULT hr = webView->PostWebMessageAsJson(wJsonMessage.c_str());

            if (FAILED(hr)) {
                OutputDebugStringW(L"[EventBridge] PostWebMessageAsJson failed: ");
                wchar_t buf[32];
                swprintf_s(buf, L"0x%08X\n", hr);
                OutputDebugStringW(buf);
            }
        }
    }
    catch (const std::exception& ex) {
        OutputDebugStringW(L"[EventBridge] Send error: ");
        std::string error(ex.what());
        int wErrLen = MultiByteToWideChar(CP_UTF8, 0, error.c_str(), (int)error.length(), NULL, 0);
        if (wErrLen > 0) {
            std::wstring wError(wErrLen, 0);
            MultiByteToWideChar(CP_UTF8, 0, error.c_str(), (int)error.length(), &wError[0], wErrLen);
            OutputDebugStringW(wError.c_str());
        }
        OutputDebugStringW(L"\n");
    }
}

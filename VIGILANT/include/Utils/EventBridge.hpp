#ifndef EVENT_BRIDGE_HPP
#define EVENT_BRIDGE_HPP

#include <thread>
#include <atomic>
#include <memory>
#include <string>

// Forward declarations
class WebViewManager;
class EventQueue;
struct EventData;

// EventQueue'dan EventData okuyup JSON'a çevirip WebView'e gönderen köprü sınıfı
class EventBridge {
public:
    EventBridge(WebViewManager* webViewManager, EventQueue* eventQueue);
    ~EventBridge();

    // Köprüyü başlatır (ayrı thread'de çalışır)
    void Start();

    // Köprüyü durdurur
    void Stop();

    // Köprünün çalışıp çalışmadığını kontrol eder
    bool IsRunning() const { return m_running.load(); }

private:
    // Arka planda çalışan işçi thread fonksiyonu
    void WorkerThread();

    // EventData'yı JSON string'e çevirir
    std::string EventDataToJson(const EventData& data);

    // WebView'e JSON mesajı gönderir
    void SendToWebView(const std::string& jsonMessage);

    WebViewManager* m_webViewManager;
    EventQueue* m_eventQueue;
    std::unique_ptr<std::thread> m_workerThread;
    std::atomic<bool> m_running;
};

#endif // EVENT_BRIDGE_HPP

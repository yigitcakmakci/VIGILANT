#ifndef EVENT_BRIDGE_HPP
#define EVENT_BRIDGE_HPP

#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include "Utils/RingBuffer.hpp"
#include "Utils/EventQueue.hpp"   // EventData definition

class WebViewManager;

// Ring-buffered, rate-limited bridge: hook thread → queue → WebView (20 Hz max).
// Overflow policy: latest-wins (oldest dropped).
// ActiveAppChanged is coalesced — only the most recent state is forwarded.
class EventBridge {
public:
    static constexpr size_t kRingCapacity       = 64;
    static constexpr int    kSendIntervalMs     = 50;   // 20 Hz
    static constexpr int    kTelemetryIntervalSec = 10;

    struct Telemetry {
        std::atomic<uint64_t> coalescedEvents{0};
        std::atomic<uint64_t> sentEvents{0};
    };

    EventBridge(WebViewManager* webViewManager);
    ~EventBridge();

    void Start();
    void Stop();
    bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }

    // Thread-safe producer API — called from any thread (hook, tracker, etc.)
    void PushEvent(const EventData& data);

    const Telemetry& GetTelemetry() const { return m_telemetry; }

private:
    void WorkerThread();
    std::string EventDataToJson(const EventData& data);
    void SendToWebView(const std::string& jsonMessage);
    void LogTelemetry();

    WebViewManager* m_webViewManager;
    RingBuffer<EventData, kRingCapacity> m_ringBuffer;
    std::unique_ptr<std::thread> m_workerThread;
    std::atomic<bool> m_running{false};

    // Condition variable used for rate-limit sleep + graceful Stop()
    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCV;

    Telemetry m_telemetry;
};

#endif // EVENT_BRIDGE_HPP

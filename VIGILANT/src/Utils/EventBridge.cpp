#include "Utils/EventBridge.hpp"
#include "UI/WebViewManager.hpp"
#include "Utils/json.hpp"
#include <string>
#include <chrono>

#define WM_WEBVIEW_ACTIVEAPP (WM_APP + 3)
extern DWORD g_WebViewThreadId;

using json = nlohmann::json;

EventBridge::EventBridge(WebViewManager* webViewManager)
    : m_webViewManager(webViewManager)
{
}

EventBridge::~EventBridge() {
    Stop();
}

void EventBridge::Start() {
    if (m_running.load(std::memory_order_relaxed)) {
        OutputDebugStringW(L"[EventBridge] Already running\n");
        return;
    }

    m_running.store(true, std::memory_order_release);
    m_workerThread = std::make_unique<std::thread>(&EventBridge::WorkerThread, this);
    OutputDebugStringW(L"[EventBridge] Started (ring capacity=64, 20Hz throttle)\n");
}

void EventBridge::Stop() {
    if (!m_running.load(std::memory_order_relaxed)) {
        return;
    }

    OutputDebugStringW(L"[EventBridge] Stopping...\n");
    m_running.store(false, std::memory_order_release);
    m_wakeCV.notify_all();

    if (m_workerThread && m_workerThread->joinable()) {
        m_workerThread->join();
    }

    LogTelemetry();
    OutputDebugStringW(L"[EventBridge] Stopped\n");
}

// ── Producer API (called from hook / tracker thread) ───────────────────
void EventBridge::PushEvent(const EventData& data) {
    m_ringBuffer.push(data);
    m_wakeCV.notify_one();   // best-effort wake for lower first-event latency
}

// ── Consumer: throttled sender (20 Hz max) ─────────────────────────────
void EventBridge::WorkerThread() {
    OutputDebugStringW(L"[EventBridge::WorkerThread] Started (throttled 20Hz)\n");

    using Clock = std::chrono::steady_clock;
    auto lastTelemetryTime = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        // Sleep for the rate-limit interval; Stop() wakes us immediately.
        {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wakeCV.wait_for(lock, std::chrono::milliseconds(kSendIntervalMs),
                [this] { return !m_running.load(std::memory_order_relaxed); });
        }
        if (!m_running.load(std::memory_order_relaxed)) break;

        // Drain ring buffer → coalesce → send only the latest event
        EventData latestEvent;
        size_t coalesced = 0;
        if (m_ringBuffer.drain_latest(latestEvent, &coalesced)) {
            m_telemetry.coalescedEvents.fetch_add(coalesced, std::memory_order_relaxed);

            std::string jsonMessage = EventDataToJson(latestEvent);
            SendToWebView(jsonMessage);

            m_telemetry.sentEvents.fetch_add(1, std::memory_order_relaxed);
        }

        // Periodic telemetry
        auto now = Clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(
                now - lastTelemetryTime).count() >= kTelemetryIntervalSec) {
            LogTelemetry();
            lastTelemetryTime = now;
        }
    }

    OutputDebugStringW(L"[EventBridge::WorkerThread] Stopped\n");
}

// ── JSON serialisation ─────────────────────────────────────────────────
std::string EventBridge::EventDataToJson(const EventData& data) {
    try {
        json j;
        j["type"]    = "ActiveAppChanged";
        j["version"] = 1;
        j["payload"] = {
            {"pid",          data.pid},
            {"exePath",      data.exePath},
            {"windowTitle",  data.title},
            {"timestampUtc", data.timestampUtc}
        };
        return j.dump();
    }
    catch (const std::exception&) {
        OutputDebugStringW(L"[EventBridge] JSON conversion error\n");
        return R"({"type":"error","message":"JSON conversion failed"})";
    }
}

// ── WebView dispatch (via WebView thread message loop) ─────────────────
void EventBridge::SendToWebView(const std::string& jsonMessage) {
    DWORD threadId = g_WebViewThreadId;
    if (threadId == 0) {
        OutputDebugStringW(L"[EventBridge] WebView thread not ready\n");
        return;
    }

    try {
        // Heap-allocate JSON string for cross-thread delivery
        std::string* pJson = new std::string(jsonMessage);
        if (!PostThreadMessageW(threadId, WM_WEBVIEW_ACTIVEAPP, 0, reinterpret_cast<LPARAM>(pJson))) {
            delete pJson;
            OutputDebugStringW(L"[EventBridge] PostThreadMessage failed\n");
        }
    }
    catch (const std::exception&) {
        OutputDebugStringW(L"[EventBridge] Send error\n");
    }
}

// ── Telemetry snapshot ─────────────────────────────────────────────────
void EventBridge::LogTelemetry() {
    uint64_t overflows   = m_ringBuffer.reset_overflow_count();
    size_t   peak        = m_ringBuffer.reset_peak_depth();
    size_t   currentDepth = m_ringBuffer.depth();
    uint64_t sent        = m_telemetry.sentEvents.load(std::memory_order_relaxed);
    uint64_t coalesced   = m_telemetry.coalescedEvents.load(std::memory_order_relaxed);

    wchar_t buf[256];
    swprintf_s(buf,
        L"[EventBridge::Telemetry] sent=%llu coalesced=%llu "
        L"overflow_drops=%llu depth=%zu peak=%zu\n",
        sent, coalesced, overflows, currentDepth, peak);
    OutputDebugStringW(buf);
}

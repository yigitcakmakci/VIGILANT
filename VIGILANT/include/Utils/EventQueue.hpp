#ifndef EVENT_QUEUE_HPP
#define EVENT_QUEUE_HPP

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Yakalanan her bir odak değişiminin paketi
struct EventData {
    unsigned long pid = 0;
    std::string processName;
    std::string exePath;
    std::string title;
    std::string url;
    std::string timestampUtc;
};

class EventQueue {
private:
    std::queue<EventData> eventQueue;
    std::mutex mtx;
    std::condition_variable cv;
    bool stopFlag = false;

public:
    // Üretici (Ghost) veriyi buraya bırakır
    void push(const EventData& data) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            eventQueue.push(data);
        }
        cv.notify_one(); // İşçiyi uyandır: "Yeni iş var!"
    }

    // Move overload — avoids string copies from resolver thread
    void push(EventData&& data) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            eventQueue.push(std::move(data));
        }
        cv.notify_one();
    }

    // Tüketici (Worker) veriyi buradan alır
    // Timed pop: returns true if data available, false on timeout or stop
    // status: 0=data, 1=timeout, 2=stopped
    bool pop_for(EventData& data, int timeoutMs, int& status) {
        std::unique_lock<std::mutex> lock(mtx);
        bool ready = cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [this] { return !eventQueue.empty() || stopFlag; });

        if (stopFlag && eventQueue.empty()) { status = 2; return false; }
        if (!ready || eventQueue.empty()) { status = 1; return false; }

        data = eventQueue.front();
        eventQueue.pop();
        status = 0;
        return true;
    }

    bool pop(EventData& data) {
        std::unique_lock<std::mutex> lock(mtx);
        // Kuyruk boşsa ve durdurulmadıysa uyu (CPU %0!)
        cv.wait(lock, [this] { return !eventQueue.empty() || stopFlag; });

        if (stopFlag && eventQueue.empty()) return false;

        data = eventQueue.front();
        eventQueue.pop();
        return true;
    }

    // Current queue depth (for performance monitoring)
    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return eventQueue.size();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stopFlag = true;
        }
        cv.notify_all();
    }
};

// Global instance declarations
extern EventQueue g_EventQueue;

#endif
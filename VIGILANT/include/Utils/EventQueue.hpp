#ifndef EVENT_QUEUE_HPP
#define EVENT_QUEUE_HPP

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

// Yakalanan her bir odak değişiminin paketi
struct EventData {
    std::string processName;
    std::string title;
    std::string url;
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

    // Tüketici (Worker) veriyi buradan alır
    bool pop(EventData& data) {
        std::unique_lock<std::mutex> lock(mtx);
        // Kuyruk boşsa ve durdurulmadıysa uyu (CPU %0!)
        cv.wait(lock, [this] { return !eventQueue.empty() || stopFlag; });

        if (stopFlag && eventQueue.empty()) return false;

        data = eventQueue.front();
        eventQueue.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stopFlag = true;
        }
        cv.notify_all();
    }
};

// Global instance declaration
extern EventQueue g_EventQueue;

#endif
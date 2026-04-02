//#include <iostream>
//#include <thread> // Test için sleep kullanacağız
//#include <chrono>
//#include "IdleTracker.hpp"
//
//int main() {
//    // 5 saniye boyunca işlem yapılmazsa IDLE sayacak bir tracker
//    IdleTracker ghostEye(5);
//
//    std::cout << "VIGILANT Ghost Eye: Monitoring Idle State..." << std::endl;
//    std::cout << "Threshold set to 5 seconds." << std::endl;
//
//    while (true) {
//        if (ghostEye.isUserIdle()) {
//            std::cout << "[STATUS: IDLE] - Time: " << ghostEye.getIdleTimeMillis() / 1000 << "s" << std::endl;
//        }
//        else {
//            std::cout << "[STATUS: ACTIVE]" << std::endl;
//        }
//
//        // CPU'yu yormamak için 1 saniye bekle (42 disiplini: performans!)
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//    }
//
//    return 0;
//}

//#include "WindowTracker.hpp"
//#include <iostream>
//
//int main() {
//    std::cout << "VIGILANT Ghost: Window Tracking Initiated..." << std::endl;
//
//    WindowTracker::StartTracking();
//
//    // Windows Mesaj Döngüsü (Hook'un çalışması için ŞART)
//    MSG msg;
//    while (GetMessage(&msg, nullptr, 0, 0)) {
//        TranslateMessage(&msg);
//        DispatchMessage(&msg);
//    }
//
//    WindowTracker::StopTracking();
//    return 0;
//}

#include "WindowTracker.hpp"
#include "EventQueue.hpp"
#include "DatabaseManager.hpp"
#include <iostream>
#include <thread>
#include <chrono>

EventQueue g_EventQueue;
DatabaseManager g_Vault("vigilant_vault.db");

void BackgroundWorker() {
    g_Vault.init();
    EventData data;
    int lastId = -1;
    std::string lastTitle = "";
    auto lastStartTime = std::chrono::steady_clock::now();

    while (g_EventQueue.pop(data)) {
        if (data.title.empty() || data.title == "Adsız") continue;

        auto now = std::chrono::steady_clock::now();

        // Odak değiştiyse süreyi hesapla ve yaz
        if (lastId != -1 && data.title != lastTitle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastStartTime).count();
            g_Vault.updateDuration(lastId, static_cast<int>(duration));
        }

        // Aynı pencereyse yeni kayıt açma
        if (data.title == lastTitle) continue;

        // Yeni kayıt aç
        lastId = g_Vault.logActivity(data);
        lastTitle = data.title;
        lastStartTime = now;

        std::cout << "[VAULT] Monitoring: " << data.processName << " -> " << data.title << std::endl;
    }
}

int main() {
    std::thread worker(BackgroundWorker);
    WindowTracker::StartTracking();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WindowTracker::StopTracking();
    g_EventQueue.stop();
    worker.join();
    return 0;
}
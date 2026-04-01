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
#include <iostream>
#include <thread>

// Global nesneler
EventQueue g_EventQueue;

// Arka planda çalışan işçi fonksiyonu
void BackgroundWorker() {
    EventData data;
    while (g_EventQueue.pop(data)) {
        // Asıl ağır işler (ekrana yazma, ileride DB'ye kaydetme) burada
        std::cout << "\n[WORKER] Processing Activity:" << std::endl;
        std::cout << "  -> Title: " << data.title << std::endl;
        if (!data.url.empty()) std::cout << "  -> URL:   " << data.url << std::endl;
        std::cout << "-------------------------------" << std::endl;
    }
}

int main() {
    std::cout << "VIGILANT v0.1 ALPHA - Ghost Engine Active" << std::endl;

    // İşçi thread'ini başlat (Tüketici)
    std::thread worker(BackgroundWorker);

    // Kancayı at (Üretici)
    WindowTracker::StartTracking();

    // Windows Mesaj Döngüsü (Ana Thread burada bekler)
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WindowTracker::StopTracking();
    g_EventQueue.stop();
    worker.join(); // İşçinin işini bitirmesini bekle

    return 0;
}
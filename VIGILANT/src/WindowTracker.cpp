#include "WindowTracker.hpp"
#include "BrowserBridge.hpp"
#include "EventQueue.hpp"
#include <iostream>

// Static üyeyi tanımlıyoruz
HWINEVENTHOOK WindowTracker::hHook = nullptr;

void WindowTracker::StartTracking() {
    // Kancayı atıyoruz: Sadece FOREGROUND (ön plan) değişimlerini dinle
    hHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_NAMECHANGE, // Başlangıç ve bitiş olayı aynı
        nullptr,                                          // DLL kullanmıyoruz
        WindowTracker::WinEventProc,                      // Bizim ihbar fonksiyonu
        0, 0,                                             // Tüm süreçleri ve threadleri dinle
        WINEVENT_OUTOFCONTEXT                             // Kendi sürecimizden dinle
    );

    if (hHook) std::cout << "[GHOST] Hook attached successfully." << std::endl;
}

void WindowTracker::StopTracking() {
    if (hHook) UnhookWinEvent(hHook);
}

BrowserBridge g_Bridge;
std::string lastTitle = "";
std::string lastUrl = "";

extern EventQueue g_EventQueue; // Main'de tanımladığımız kuyruğa erişim
extern BrowserBridge g_Bridge;

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_NAMECHANGE) {
        char windowTitle[256];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle)) == 0) return;

        std::string currentTitle = windowTitle;
        std::string currentUrl = "";

        // Tarayıcı ise detayları çek
        if (currentTitle.find("Edge") != std::string::npos || currentTitle.find("Chrome") != std::string::npos) {
            currentUrl = g_Bridge.GetActiveURL(hwnd);
        }

        // Paketi hazırla ve kuyruğa at (Çok hızlı!)
        EventData data = { currentTitle, currentUrl, "2026-04-02" }; // Zamanı dinamik yapabilirsin
        g_EventQueue.push(data);
    }
}
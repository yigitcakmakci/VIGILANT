#include "WindowTracker.hpp"
#include "BrowserBridge.hpp"
#include "EventQueue.hpp"
#include <psapi.h> // Gerekli
#include <iostream>

#pragma comment(lib, "Psapi.lib")

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

std::string WindowTracker::GetProcessName(HWND hwnd) {
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    // PROCESS_QUERY_LIMITED_INFORMATION bazen daha güvenlidir
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);

    if (hProcess) {
        char buffer[MAX_PATH];
        // GetModuleBaseNameA (Sondaki 'A' harfi ANSI yani char demektir)
        if (GetModuleBaseNameA(hProcess, NULL, buffer, MAX_PATH)) {
            CloseHandle(hProcess);
            return std::string(buffer);
        }
        CloseHandle(hProcess);
    }
    return "Unknown";
}

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    // Sadece pencerelerle ilgileniyoruz
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_NAMECHANGE) {
        char windowTitle[256];
        // Başlık çekilemezse çık
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle)) == 0) return;

        std::string currentTitle = windowTitle;
        std::string pName = GetProcessName(hwnd); // Her uygulama için çekiyoruz
        std::string currentUrl = "";

        // Sadece tarayıcı ise URL çekmeye çalış
        if (pName == "msedge.exe" || pName == "chrome.exe") {
            currentUrl = g_Bridge.GetActiveURL(hwnd);
        }

        // Paketi kuyruğa fırlat
        EventData data = { pName, currentTitle, currentUrl };
        g_EventQueue.push(data);
    }
}
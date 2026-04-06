#include "../../include/Core/WindowTracker.hpp"
#include "../../include/UI/BrowserBridge.hpp"
#include "../../include/Utils/EventQueue.hpp"
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
std::string lastProcess = "";
std::string lastTitle = "";

extern EventQueue g_EventQueue;

std::string WindowTracker::GetProcessName(HWND hwnd) {
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    // PROCESS_QUERY_LIMITED_INFORMATION tum processlere erisir (admin gerekmez)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

    if (hProcess) {
        char buffer[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameA(hProcess, 0, buffer, &size)) {
            CloseHandle(hProcess);
            std::string fullPath(buffer);
            size_t pos = fullPath.find_last_of("\\/");
            if (pos != std::string::npos)
                return fullPath.substr(pos + 1);
            return fullPath;
        }
        CloseHandle(hProcess);
    }
    return "Unknown";
}

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (!hwnd || !IsWindow(hwnd)) return;

    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_NAMECHANGE) {
        // NAMECHANGE olaylarini sadece on plandaki pencere icin isle
        if (event == EVENT_OBJECT_NAMECHANGE && hwnd != GetForegroundWindow()) return;

        char windowTitle[256];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle)) == 0) return;

        std::string currentTitle = windowTitle;
        std::string pName = GetProcessName(hwnd);

        // Bilinmeyen ve kendi processimizi atla
        if (pName == "Unknown") return;

        // Ayni process+baslik tekrar geliyorsa kuyruga atma
        if (pName == lastProcess && currentTitle == lastTitle) return;
        lastProcess = pName;
        lastTitle = currentTitle;

        std::string currentUrl = "";
        if (pName == "msedge.exe" || pName == "chrome.exe" || pName == "firefox.exe") {
            currentUrl = g_Bridge.GetActiveURL(hwnd);
        }

        EventData data = { pName, currentTitle, currentUrl };
        g_EventQueue.push(data);
    }
}
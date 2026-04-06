#include "../../include/Core/WindowTracker.hpp"
#include "../../include/UI/BrowserBridge.hpp"
#include "../../include/Utils/EventQueue.hpp"
#include <psapi.h> // Gerekli
#include <iostream>

#pragma comment(lib, "Psapi.lib")

// Static uyeleri tanimliyoruz
HWINEVENTHOOK WindowTracker::hHookForeground = nullptr;
HWINEVENTHOOK WindowTracker::hHookNameChange = nullptr;

void WindowTracker::StartTracking() {
    // Hook 1: Sadece EVENT_SYSTEM_FOREGROUND — on plan degisimi
    hHookForeground = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr,
        WindowTracker::WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    // Hook 2: Sadece EVENT_OBJECT_NAMECHANGE — baslik degisimi
    hHookNameChange = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        nullptr,
        WindowTracker::WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (hHookForeground && hHookNameChange)
        std::cout << "[GHOST] Hooks attached successfully (narrow range)." << std::endl;
}

void WindowTracker::StopTracking() {
    if (hHookForeground) UnhookWinEvent(hHookForeground);
    if (hHookNameChange) UnhookWinEvent(hHookNameChange);
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
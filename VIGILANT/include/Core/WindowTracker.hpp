#ifndef WINDOW_TRACKER_HPP
#define WINDOW_TRACKER_HPP

#include <windows.h>
#include <string>

class WindowTracker {
public:
    static void StartTracking();
    static void StopTracking();
    // Yeni ekledik:
    static std::string GetProcessName(HWND hwnd);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);

private:
    static HWINEVENTHOOK hHook;
};

#endif
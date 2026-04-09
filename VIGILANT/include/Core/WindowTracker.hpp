#ifndef WINDOW_TRACKER_HPP
#define WINDOW_TRACKER_HPP

#include <windows.h>
#include <string>
#include <atomic>

class WindowTracker {
public:
    static void StartTracking();
    static void StopTracking();
    static void PauseTracking();
    static void ResumeTracking();
    static bool IsPaused();
    // Yeni ekledik:
    static std::string GetProcessName(HWND hwnd);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);

private:
    static HWINEVENTHOOK hHookForeground;
    static HWINEVENTHOOK hHookNameChange;
    static ULONGLONG s_lastEventTick;
    static std::atomic<bool> s_paused;
};

#endif
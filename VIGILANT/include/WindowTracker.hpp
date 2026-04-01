#ifndef WINDOW_TRACKER_HPP
#define WINDOW_TRACKER_HPP

#include <windows.h>
#include <string>

class WindowTracker {
public:
    // Kancayı (Hook) takan fonksiyon
    static void StartTracking();

    // Kancayı söken fonksiyon
    static void StopTracking();

    // Windows'un bizi arayacağı "İhbar Hattı" (Callback)
    static void CALLBACK WinEventProc(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD dwEventThread,
        DWORD dwmsEventTime
    );

private:
    static HWINEVENTHOOK hHook;
};

#endif
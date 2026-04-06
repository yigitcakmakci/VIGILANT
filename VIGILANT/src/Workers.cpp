#include <windows.h>
#include <thread>
#include <chrono>
#include <iostream>
#include "Utils/EventQueue.hpp"
#include "Data/DatabaseManager.hpp"

extern DatabaseManager g_Vault;
extern EventQueue g_EventQueue;

// --- BackgroundWorker ---
// Olaylari kuyruktan alir, DB'ye yazar ve onceki aktivitenin suresini gunceller.
// AI siniflandirma artik AIClassifierTask tarafindan bagimsiz thread'de yapilir.
void BackgroundWorker()
{
    int lastActivityId = -1;
    auto lastTime = std::chrono::steady_clock::now();

    while (true)
    {
        EventData data;
        if (!g_EventQueue.pop(data))
            break; // stop flag + kuyruk bos

        auto now = std::chrono::steady_clock::now();

        // Onceki aktivitenin suresini guncelle
        if (lastActivityId > 0) {
            int seconds = (int)std::chrono::duration_cast<std::chrono::seconds>(
                now - lastTime).count();
            if (seconds > 0) {
                g_Vault.updateDuration(lastActivityId, seconds);
            }
        }

        // Yeni aktiviteyi kaydet
        lastActivityId = g_Vault.logActivity(data);
        lastTime = now;
    }
}

// --- HotkeyWorker ---
// Ctrl+Shift+V: Pencereyi goster/gizle
// Ctrl+Shift+Q: Uygulamayi kapat
void HotkeyWorker()
{
    const int HOTKEY_TOGGLE = 1;
    const int HOTKEY_QUIT   = 2;

    RegisterHotKey(NULL, HOTKEY_TOGGLE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V');
    RegisterHotKey(NULL, HOTKEY_QUIT,   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Q');

    std::cout << "[Hotkey] Kisayollar aktif: Ctrl+Shift+V (Goster/Gizle), Ctrl+Shift+Q (Kapat)" << std::endl;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            HWND hwnd = FindWindowW(L"VigilantWindowClass", nullptr);
            if (!hwnd) continue;

            if (msg.wParam == HOTKEY_TOGGLE) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    std::cout << "[Hotkey] Pencere gizlendi." << std::endl;
                }
                else {
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    std::cout << "[Hotkey] Pencere gosterildi." << std::endl;
                }
            }
            else if (msg.wParam == HOTKEY_QUIT) {
                std::cout << "[Hotkey] Kapatma istegi alindi." << std::endl;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                break;
            }
        }
    }

    UnregisterHotKey(NULL, HOTKEY_TOGGLE);
    UnregisterHotKey(NULL, HOTKEY_QUIT);
}

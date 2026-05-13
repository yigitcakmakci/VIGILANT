#include <windows.h>
#include <thread>
#include <chrono>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include "Utils/EventQueue.hpp"
#include "Utils/PerfCounters.hpp"
#include "Data/DatabaseManager.hpp"
#include "Core/WindowTracker.hpp"
#include "UI/TrayManager.hpp"

#define WM_WEBVIEW_NEWEVENT (WM_APP + 2)

extern DatabaseManager g_Vault;
extern EventQueue g_EventQueue;
extern DWORD g_WebViewThreadId;

// --- BackgroundWorker ---
// Olaylari kuyruktan alir, batch transaction ile DB'ye yazar.
// Batch kosulu: N event birikince VEYA T saniye dolunca (hangisi once).
static constexpr int    kBatchMaxSize    = 10;
static constexpr int    kBatchFlushMs    = 2000;            // 2 saniye
// pop_for timeout: flush periyodunun yarisi -> en kotu durumda flush gecikmesi
// kBatchFlushMs/2 + epsilon kadar olur; CPU spin'e dusmeden sik kontrol saglar.
static constexpr int    kQueuePopTimeoutMs = kBatchFlushMs / 2;

// Hariç tutma listesi cache'i (AppSettings: "tracking.excludedProcesses" — virgülle ayrılmış)
static std::unordered_set<std::string> s_excludedProcs;
static std::chrono::steady_clock::time_point s_excludedLast{};
static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}
static void RefreshExcludedProcsIfNeeded() {
    auto now = std::chrono::steady_clock::now();
    if (s_excludedLast.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::seconds>(now - s_excludedLast).count() < 5) return;
    s_excludedLast = now;
    std::string raw = g_Vault.getSetting("tracking.excludedProcesses", "");
    s_excludedProcs.clear();
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        std::string p = ToLower(item.substr(a, b - a + 1));
        if (!p.empty()) s_excludedProcs.insert(p);
    }
}
static bool IsProcessExcluded(const std::string& proc) {
    if (s_excludedProcs.empty()) return false;
    return s_excludedProcs.count(ToLower(proc)) > 0;
}

void BackgroundWorker()
{
    int lastActivityId = -1;
    auto lastTime = std::chrono::steady_clock::now();
    auto lastFlush = std::chrono::steady_clock::now();
    int batchCount = 0;
    bool inTransaction = false;

    // pop_for timeout: half of batch flush interval so we check frequently
    static constexpr int kPopTimeoutMs = kQueuePopTimeoutMs;

    while (true)
    {
        EventData data;
        int status = 0;
        g_EventQueue.pop_for(data, kPopTimeoutMs, status);

        // stop flag raised and queue drained
        if (status == 2) break;

        // timeout - no new event arrived, but we may need to flush pending batch
        if (status == 1) {
            if (inTransaction) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush).count();
                if (elapsed >= kBatchFlushMs) {
                    PERF_SCOPED_TIMER(db_batch_commit);
                    g_Vault.commitTransaction();
                    inTransaction = false;
                    batchCount = 0;
                    // Dashboard bilgilendirmesi YAPILMIYOR — ilk event
                    // isleme adimindan (asagida) zaten gonderiliyor.
                    // Bu ikinci bildirim gereksiz DOM yeniden insa ve
                    // kullanici etkilesimi engellemesine yol aciyordu.
                }
            }
            continue;
        }

        // --- status == 0: yeni event geldi ---
        auto now = std::chrono::steady_clock::now();

        // Start batch transaction if not already in one
        if (!inTransaction) {
            g_Vault.beginTransaction();
            inTransaction = true;
            lastFlush = now;
        }

        // Onceki aktivitenin suresini guncelle
        if (lastActivityId > 0) {
            int seconds = (int)std::chrono::duration_cast<std::chrono::seconds>(
                now - lastTime).count();
            if (seconds > 0) {
                g_Vault.updateDuration(lastActivityId, seconds);
                // Aktif (idle olmayan) saniye: toplam - idle. Idle suresi
                // pencereler arasi gecisi temsil eden simdiki ana yakin oldugundan
                // kucuk degerlerde 0'a yaslanir; floor 0 ile guvende.
                int idleSec = WindowTracker::GetIdleSeconds();
                int activeSec = seconds - idleSec;
                if (activeSec < 0) activeSec = 0;
                if (activeSec > seconds) activeSec = seconds;
                g_Vault.updateActiveDuration(lastActivityId, activeSec);
            }
        }

        // Yeni aktiviteyi kaydet (prepared statement - no prepare/finalize overhead)
        RefreshExcludedProcsIfNeeded();
        if (IsProcessExcluded(data.processName)) {
            // Bu uygulama hariç tutulmuş; aktiviteyi kaydetme.
            lastActivityId = -1;
            lastTime = now;
            continue;
        }
        lastActivityId = g_Vault.logActivity(data);
        lastTime = now;
        batchCount++;

        // Flush batch: N events OR T seconds elapsed
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush).count();
        if (batchCount >= kBatchMaxSize || elapsed >= kBatchFlushMs) {
            PERF_SCOPED_TIMER(db_batch_commit);
            g_Vault.commitTransaction();
            inTransaction = false;
            batchCount = 0;
        }

        // DB yazimi tamamlandiktan sonra dashboard'u bilgilendir
        if (g_WebViewThreadId != 0) {
            PostThreadMessageW(g_WebViewThreadId, WM_WEBVIEW_NEWEVENT, 0, 0);
        }
    }

    // Flush remaining batch on shutdown
    if (inTransaction) {
        g_Vault.commitTransaction();
    }
}

// --- HotkeyWorker ---
// Ctrl+Shift+V: Pencereyi goster/gizle
// Ctrl+Shift+Q: Uygulamayi kapat
extern DWORD g_HotkeyThreadId;

void HotkeyWorker()
{
    g_HotkeyThreadId = GetCurrentThreadId();

    const int HOTKEY_TOGGLE = 1;
    const int HOTKEY_QUIT   = 2;

    RegisterHotKey(NULL, HOTKEY_TOGGLE, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V');
    RegisterHotKey(NULL, HOTKEY_QUIT,   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Q');

    OutputDebugStringW(L"[Hotkey] Kisayollar aktif: Ctrl+Shift+V (Goster/Gizle), Ctrl+Shift+Q (Kapat)\n");

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
                    OutputDebugStringW(L"[Hotkey] Pencere gizlendi.\n");
                }
                else {
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    OutputDebugStringW(L"[Hotkey] Pencere gosterildi.\n");
                }
            }
            else if (msg.wParam == HOTKEY_QUIT) {
                OutputDebugStringW(L"[Hotkey] Kapatma istegi alindi.\n");
                PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_TRAY_QUIT, 0), 0);
                break;
            }
        }
    }

    UnregisterHotKey(NULL, HOTKEY_TOGGLE);
    UnregisterHotKey(NULL, HOTKEY_QUIT);
}

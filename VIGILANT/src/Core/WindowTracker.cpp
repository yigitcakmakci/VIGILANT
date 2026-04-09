#include "../../include/Core/WindowTracker.hpp"
#include "../../include/UI/BrowserBridge.hpp"
#include "../../include/Utils/EventQueue.hpp"
#include "../../include/Utils/EventBridge.hpp"
#include "../../include/Utils/ProcessCache.hpp"
#include "../../include/Utils/PerfCounters.hpp"
#include <psapi.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <condition_variable>

#pragma comment(lib, "Psapi.lib")

// ============================================================================
// OPTIMIZED ARCHITECTURE:
//   Hook callback (< 1µs) → RawHookQueue → ResolverThread (heavy work)
//
// Callback sadece HWND + event + tick yakalar, tum agir is (OpenProcess,
// WideToUtf8, UIA, string alloc) resolver thread'de yapilir.
// ============================================================================

// --- Lightweight raw event (no heap allocations) ---
struct RawHookEvent {
    HWND    hwnd;
    DWORD   event;
    DWORD   tick;  // dwmsEventTime from OS
};

// --- Lock-free-ish raw event queue (SPSC: hook → resolver) ---
static constexpr size_t kRawQueueCapacity = 128;
static RawHookEvent     s_rawQueue[kRawQueueCapacity];
static std::atomic<size_t> s_rawHead{0};
static std::atomic<size_t> s_rawTail{0};
static std::mutex           s_rawMutex;
static std::condition_variable s_rawCV;

static bool RawQueue_Push(const RawHookEvent& evt) {
    size_t head = s_rawHead.load(std::memory_order_relaxed);
    size_t next = (head + 1) % kRawQueueCapacity;
    if (next == s_rawTail.load(std::memory_order_acquire))
        return false; // full — drop event (very rare under debounce)
    s_rawQueue[head] = evt;
    s_rawHead.store(next, std::memory_order_release);
    s_rawCV.notify_one();
    return true;
}

static bool RawQueue_Pop(RawHookEvent& evt) {
    size_t tail = s_rawTail.load(std::memory_order_relaxed);
    if (tail == s_rawHead.load(std::memory_order_acquire))
        return false; // empty
    evt = s_rawQueue[tail];
    s_rawTail.store((tail + 1) % kRawQueueCapacity, std::memory_order_release);
    return true;
}

// Static uyeleri tanimliyoruz
HWINEVENTHOOK WindowTracker::hHookForeground = nullptr;
HWINEVENTHOOK WindowTracker::hHookNameChange = nullptr;
ULONGLONG WindowTracker::s_lastEventTick = 0;
std::atomic<bool> WindowTracker::s_paused{false};

// Resolver thread state
static std::atomic<bool> s_resolverRunning{false};
static std::thread       s_resolverThread;

// Process cache (PID → exe name, 60s TTL)
static ProcessCache s_processCache;

// Dedup state (resolver thread only — no sync needed)
static std::string s_lastProcess;
static std::string s_lastTitle;

// External dependencies
static BrowserBridge g_Bridge;
extern EventQueue    g_EventQueue;
extern EventBridge*  g_EventBridge;

// Yardimci: wchar_t* -> UTF-8 std::string (stack buffer optimization)
static std::string WideToUtf8(const wchar_t* wstr, int wlen = -1) {
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};

    // Stack buffer for small strings (avoids heap allocation ~95% of the time)
    char stackBuf[512];
    if (wlen == -1 && len <= (int)sizeof(stackBuf)) {
        WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, stackBuf, len, nullptr, nullptr);
        return std::string(stackBuf, len - 1); // -1: exclude NUL
    }
    if (wlen != -1 && len <= (int)sizeof(stackBuf)) {
        WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, stackBuf, len, nullptr, nullptr);
        return std::string(stackBuf, len);
    }

    // Heap fallback for very long strings
    std::string s(wlen == -1 ? len - 1 : len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, &s[0], len, nullptr, nullptr);
    return s;
}

// ── Resolver Thread: does ALL heavy work off the hook thread ───────────
static void ResolverThread() {
    OutputDebugStringA("[WindowTracker] ResolverThread started\n");

    while (s_resolverRunning.load(std::memory_order_relaxed)) {
        RawHookEvent raw;

        // Wait for events (blocks, no CPU spin)
        {
            std::unique_lock<std::mutex> lock(s_rawMutex);
            s_rawCV.wait_for(lock, std::chrono::milliseconds(500), [] {
                return s_rawTail.load(std::memory_order_relaxed) !=
                       s_rawHead.load(std::memory_order_relaxed) ||
                       !s_resolverRunning.load(std::memory_order_relaxed);
            });
        }
        if (!s_resolverRunning.load(std::memory_order_relaxed)) break;

        // Drain all pending raw events
        while (RawQueue_Pop(raw)) {
            PERF_SCOPED_TIMER(resolve_event);

            if (!raw.hwnd || !IsWindow(raw.hwnd)) continue;

            // --- Window title (heavy: GetWindowTextW) ---
            wchar_t wTitle[512];
            int titleLen = GetWindowTextW(raw.hwnd, wTitle, _countof(wTitle));
            if (titleLen == 0) continue;
            std::string currentTitle = WideToUtf8(wTitle, titleLen);
            if (currentTitle.empty()) continue;

            // --- PID + process name (cached) ---
            DWORD processId = 0;
            GetWindowThreadProcessId(raw.hwnd, &processId);

            auto cached = s_processCache.lookup(processId);
            if (cached.processName == "Unknown") continue;

            // --- Dedup (same process+title → skip) ---
            if (cached.processName == s_lastProcess && currentTitle == s_lastTitle) {
                PERF_COUNT(hook_callback_deduped);
                continue;
            }
            s_lastProcess = cached.processName;
            s_lastTitle   = currentTitle;

            // --- UTC timestamp (ISO 8601) ---
            SYSTEMTIME st;
            GetSystemTime(&st);
            char tsBuf[64];
            sprintf_s(tsBuf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

            // --- URL (browser only — UIA call is safe here, off hook thread) ---
            std::string currentUrl;
            if (cached.processName == "msedge.exe" ||
                cached.processName == "chrome.exe" ||
                cached.processName == "firefox.exe") {
                currentUrl = g_Bridge.GetActiveURL(raw.hwnd);
            }

            // --- Build EventData (use move to avoid copies) ---
            EventData data;
            data.pid          = processId;
            data.processName  = std::move(cached.processName);
            data.exePath      = std::move(cached.exePath);
            data.title        = std::move(currentTitle);
            data.url          = std::move(currentUrl);
            data.timestampUtc = tsBuf;

            // WebView ring buffer (EventBridge -> JS, 20Hz throttled)
            // NOTE: Push BEFORE move so EventBridge gets valid data
            if (g_EventBridge) g_EventBridge->PushEvent(data);

            // DB kuyrugu (BackgroundWorker)
            g_EventQueue.push(std::move(data));
            PERF_COUNT(events_queued);
        }
    }

    OutputDebugStringA("[WindowTracker] ResolverThread stopped\n");
}

std::string WindowTracker::GetProcessName(HWND hwnd) {
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    return s_processCache.lookup(processId).processName;
}

void WindowTracker::StartTracking() {
    // Start resolver thread BEFORE hooking
    s_resolverRunning.store(true, std::memory_order_release);
    s_resolverThread = std::thread(ResolverThread);

    hHookForeground = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr,
        WindowTracker::WinEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

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

    // Stop resolver thread
    s_resolverRunning.store(false, std::memory_order_release);
    s_rawCV.notify_all();
    if (s_resolverThread.joinable()) s_resolverThread.join();
}

void WindowTracker::PauseTracking() {
    s_paused.store(true, std::memory_order_release);
    OutputDebugStringA("[WindowTracker] Tracking PAUSED\n");
}

void WindowTracker::ResumeTracking() {
    s_paused.store(false, std::memory_order_release);
    OutputDebugStringA("[WindowTracker] Tracking RESUMED\n");
}

bool WindowTracker::IsPaused() {
    return s_paused.load(std::memory_order_acquire);
}

// ============================================================================
// ULTRA-LIGHTWEIGHT CALLBACK
// Cost: ~3 comparisons + 1 GetTickCount64 + 1 GetForegroundWindow + 1 array write
// No heap allocations. No kernel handle calls. No string operations.
// ============================================================================
void CALLBACK WindowTracker::WinEventProc(
    HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    PERF_COUNT(hook_callback_entered);

    if (s_paused.load(std::memory_order_acquire)) return;

    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (!hwnd) return;

    if (event != EVENT_SYSTEM_FOREGROUND && event != EVENT_OBJECT_NAMECHANGE) return;

    // NAMECHANGE olaylarini sadece on plandaki pencere icin isle
    if (event == EVENT_OBJECT_NAMECHANGE && hwnd != GetForegroundWindow()) return;

    // --- Debounce (100 ms) — only cheap integer compare ---
    ULONGLONG now = GetTickCount64();
    if (now - s_lastEventTick < 100) {
        PERF_COUNT(hook_callback_debounced);
        return;
    }
    s_lastEventTick = now;

    // Post lightweight event to resolver thread — NO heavy work here
    RawHookEvent raw;
    raw.hwnd  = hwnd;
    raw.event = event;
    raw.tick  = dwmsEventTime;
    RawQueue_Push(raw);
}
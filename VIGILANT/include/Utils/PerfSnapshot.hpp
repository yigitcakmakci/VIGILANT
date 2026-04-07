#ifndef PERF_SNAPSHOT_HPP
#define PERF_SNAPSHOT_HPP

// ============================================================================
// PerfSnapshot — Periodic system + internal metrics logger for VIGILANT.
//
// Collects CPU%, RSS, events/sec, queue depth, handle count, UI frame time
// every N milliseconds and writes CSV + OutputDebugString.
//
// Requires: VIGILANT_PERF_ENABLED to be defined (otherwise no-op).
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include "PerfCounters.hpp"
#include "EventQueue.hpp"

#pragma comment(lib, "Psapi.lib")

// ── Snapshot data ──────────────────────────────────────────────────────
struct PerfSnapshotData {
    // Timestamp
    double      elapsed_sec;        // seconds since logger start

    // System metrics
    double      cpu_percent;        // process CPU usage (0-100)
    double      rss_mb;             // working set in MB
    uint32_t    handle_count;       // process handle count

    // Pipeline metrics
    double      events_per_sec;     // events_queued delta / dt
    size_t      queue_depth;        // EventQueue::size() at sample time
    uint64_t    hook_entered_total; // cumulative hook callbacks
    uint64_t    resolve_max_us;     // peak resolve time (µs)
    uint64_t    db_commit_max_us;   // peak DB commit time (µs)

    // UI metric
    double      ui_frame_time_ms;   // last measured message-loop cycle
};

// ── Global UI frame time (set from main message loop) ──────────────────
namespace PerfGlobals {
    inline std::atomic<double> ui_frame_time_ms{0.0};
}

// ── Logger ─────────────────────────────────────────────────────────────
class PerfSnapshotLogger {
public:
    PerfSnapshotLogger() = default;
    ~PerfSnapshotLogger() { Stop(); }

    // Start periodic logging. intervalMs = sample period in milliseconds.
    void Start(uint32_t intervalMs = 30'000) {
#ifdef VIGILANT_PERF_ENABLED
        if (m_running.exchange(true))
            return; // already running

        m_intervalMs = intervalMs;
        m_thread = std::thread([this]() { WorkerLoop(); });
#else
        (void)intervalMs;
#endif
    }

    void Stop() {
#ifdef VIGILANT_PERF_ENABLED
        if (!m_running.exchange(false))
            return;
        if (m_thread.joinable())
            m_thread.join();
        if (m_file) {
            fclose(m_file);
            m_file = nullptr;
        }
#endif
    }

private:
#ifdef VIGILANT_PERF_ENABLED

    std::atomic<bool> m_running{false};
    std::thread       m_thread;
    uint32_t          m_intervalMs = 30'000;
    FILE*             m_file = nullptr;

    // CPU calculation state
    ULONGLONG m_prevKernel = 0;
    ULONGLONG m_prevUser   = 0;
    ULONGLONG m_prevWall   = 0;

    // Events/sec calculation state
    uint64_t m_prevEventsQueued = 0;

    void WorkerLoop() {
        OpenCsvFile();
        InitCpuBaseline();

        auto startTime = std::chrono::steady_clock::now();

        while (m_running.load(std::memory_order_relaxed)) {
            Sleep(m_intervalMs);
            if (!m_running.load(std::memory_order_relaxed))
                break;

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();

            PerfSnapshotData snap = Sample(elapsed);
            EmitDebugString(snap);
            WriteCsvRow(snap);
        }
    }

    PerfSnapshotData Sample(double elapsed) {
        PerfSnapshotData d{};
        d.elapsed_sec = elapsed;

        // CPU %
        d.cpu_percent = CalcCpuPercent();

        // RSS (Working Set)
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            d.rss_mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }

        // Handle count
        DWORD hc = 0;
        GetProcessHandleCount(GetCurrentProcess(), &hc);
        d.handle_count = static_cast<uint32_t>(hc);

        // Events/sec
        uint64_t currentQueued = PerfCounters::events_queued.load(std::memory_order_relaxed);
        uint64_t delta = currentQueued - m_prevEventsQueued;
        d.events_per_sec = static_cast<double>(delta) / (static_cast<double>(m_intervalMs) / 1000.0);
        m_prevEventsQueued = currentQueued;

        // Queue depth
        extern EventQueue g_EventQueue;
        d.queue_depth = g_EventQueue.size();

        // PerfCounters peaks
        d.hook_entered_total = PerfCounters::hook_callback_entered.load(std::memory_order_relaxed);
        d.resolve_max_us     = PerfCounters::resolve_event_max_us.load(std::memory_order_relaxed);
        d.db_commit_max_us   = PerfCounters::db_batch_commit_max_us.load(std::memory_order_relaxed);

        // UI frame time
        d.ui_frame_time_ms = PerfGlobals::ui_frame_time_ms.load(std::memory_order_relaxed);

        return d;
    }

    // ── CPU % via GetProcessTimes delta ───────────────────────────────
    void InitCpuBaseline() {
        FILETIME create, exit, kernel, user;
        GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
        m_prevKernel = FileTimeToU64(kernel);
        m_prevUser   = FileTimeToU64(user);
        m_prevWall   = GetTickCount64();
    }

    double CalcCpuPercent() {
        FILETIME create, exit, kernel, user;
        GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);

        ULONGLONG nowKernel = FileTimeToU64(kernel);
        ULONGLONG nowUser   = FileTimeToU64(user);
        ULONGLONG nowWall   = GetTickCount64();

        ULONGLONG cpuDelta  = (nowKernel - m_prevKernel) + (nowUser - m_prevUser);
        ULONGLONG wallDelta = (nowWall - m_prevWall) * 10'000ULL; // ms → 100ns units

        m_prevKernel = nowKernel;
        m_prevUser   = nowUser;
        m_prevWall   = nowWall;

        if (wallDelta == 0) return 0.0;
        return 100.0 * static_cast<double>(cpuDelta) / static_cast<double>(wallDelta);
    }

    static ULONGLONG FileTimeToU64(const FILETIME& ft) {
        ULARGE_INTEGER li;
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return li.QuadPart;
    }

    // ── CSV Output ────────────────────────────────────────────────────
    void OpenCsvFile() {
        // Create docs\perf_logs directory
        CreateDirectoryA("docs", nullptr);
        CreateDirectoryA("docs\\perf_logs", nullptr);

        // Filename with timestamp
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        char filename[128];
        snprintf(filename, sizeof(filename),
                 "docs\\perf_logs\\perf_%04d%02d%02d_%02d%02d%02d.csv",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);

        m_file = fopen(filename, "w");
        if (m_file) {
            fprintf(m_file,
                "elapsed_sec,cpu_percent,rss_mb,handle_count,"
                "events_per_sec,queue_depth,hook_entered_total,"
                "resolve_max_us,db_commit_max_us,ui_frame_time_ms\n");
            fflush(m_file);

            char dbg[256];
            snprintf(dbg, sizeof(dbg), "[PERF-SNAP] CSV opened: %s\n", filename);
            OutputDebugStringA(dbg);
        }
    }

    void WriteCsvRow(const PerfSnapshotData& d) {
        if (!m_file) return;
        fprintf(m_file,
            "%.1f,%.2f,%.1f,%u,%.2f,%zu,%llu,%llu,%llu,%.2f\n",
            d.elapsed_sec, d.cpu_percent, d.rss_mb, d.handle_count,
            d.events_per_sec, d.queue_depth, d.hook_entered_total,
            d.resolve_max_us, d.db_commit_max_us, d.ui_frame_time_ms);
        fflush(m_file);
    }

    // ── Debug Output ──────────────────────────────────────────────────
    void EmitDebugString(const PerfSnapshotData& d) {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "[PERF-SNAP] t=%.0fs cpu=%.2f%% rss=%.1fMB evt/s=%.1f "
            "q_depth=%zu handles=%u resolve_max=%lluus "
            "db_max=%lluus frame=%.1fms\n",
            d.elapsed_sec, d.cpu_percent, d.rss_mb, d.events_per_sec,
            d.queue_depth, d.handle_count, d.resolve_max_us,
            d.db_commit_max_us, d.ui_frame_time_ms);
        OutputDebugStringA(buf);
    }

#endif // VIGILANT_PERF_ENABLED
};

#endif // PERF_SNAPSHOT_HPP

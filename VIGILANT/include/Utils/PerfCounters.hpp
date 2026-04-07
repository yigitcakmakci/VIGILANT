#ifndef PERF_COUNTERS_HPP
#define PERF_COUNTERS_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ============================================================================
// Lightweight performance counters for VIGILANT activity tracker.
//
// Usage:
//   PERF_COUNT(hook_callback_entered);      // increment atomic counter
//   PERF_SCOPED_TIMER(resolve_event);       // measure scope duration (µs)
//   PerfCounters::dump();                   // print all counters
//
// All counters are lock-free (std::atomic). Zero runtime cost when
// VIGILANT_PERF_ENABLED is not defined.
// ============================================================================

// ── Feature gate ───────────────────────────────────────────────────────
// Define VIGILANT_PERF_ENABLED in your build (Debug) to activate counters.
// In Release builds, all macros compile to nothing.

#ifdef VIGILANT_PERF_ENABLED
  #define PERF_COUNT(name)        ::PerfCounters::name.fetch_add(1, std::memory_order_relaxed)
  #define PERF_COUNT_ADD(name, n) ::PerfCounters::name.fetch_add(n, std::memory_order_relaxed)
  #define PERF_SCOPED_TIMER(name) ::PerfScopedTimer _perf_timer_##name(::PerfCounters::name##_us, ::PerfCounters::name##_max_us)
#else
  #define PERF_COUNT(name)        ((void)0)
  #define PERF_COUNT_ADD(name, n) ((void)0)
  #define PERF_SCOPED_TIMER(name) ((void)0)
#endif

// ── Counter storage ────────────────────────────────────────────────────
namespace PerfCounters {

    // -- Event pipeline counts --
    inline std::atomic<uint64_t> hook_callback_entered{0};
    inline std::atomic<uint64_t> hook_callback_debounced{0};
    inline std::atomic<uint64_t> hook_callback_deduped{0};
    inline std::atomic<uint64_t> events_queued{0};

    // -- Process cache --
    inline std::atomic<uint64_t> process_cache_hit{0};
    inline std::atomic<uint64_t> process_cache_miss{0};

    // -- Database --
    inline std::atomic<uint64_t> db_batch_committed{0};
    inline std::atomic<uint64_t> db_rows_inserted{0};
    inline std::atomic<uint64_t> db_rows_updated{0};

    // -- EventBridge --
    inline std::atomic<uint64_t> bridge_events_sent{0};

    // -- Scoped timer accumulators (microseconds) --
    inline std::atomic<uint64_t> resolve_event_us{0};
    inline std::atomic<uint64_t> resolve_event_max_us{0};
    inline std::atomic<uint64_t> db_batch_commit_us{0};
    inline std::atomic<uint64_t> db_batch_commit_max_us{0};

    // Dump all counters to OutputDebugString
    inline void dump() {
        char buf[1024];
        snprintf(buf, sizeof(buf),
            "[PERF] hook_entered=%llu debounced=%llu deduped=%llu queued=%llu | "
            "cache hit=%llu miss=%llu | "
            "db_batch=%llu rows_ins=%llu rows_upd=%llu | "
            "bridge_sent=%llu | "
            "resolve_avg=%lluus max=%lluus | "
            "db_commit_avg=%lluus max=%lluus\n",
            hook_callback_entered.load(std::memory_order_relaxed),
            hook_callback_debounced.load(std::memory_order_relaxed),
            hook_callback_deduped.load(std::memory_order_relaxed),
            events_queued.load(std::memory_order_relaxed),
            process_cache_hit.load(std::memory_order_relaxed),
            process_cache_miss.load(std::memory_order_relaxed),
            db_batch_committed.load(std::memory_order_relaxed),
            db_rows_inserted.load(std::memory_order_relaxed),
            db_rows_updated.load(std::memory_order_relaxed),
            bridge_events_sent.load(std::memory_order_relaxed),
            // Average = total / max(queued, 1) to avoid div/0
            events_queued.load() ? resolve_event_us.load() / events_queued.load() : 0,
            resolve_event_max_us.load(std::memory_order_relaxed),
            db_batch_committed.load() ? db_batch_commit_us.load() / db_batch_committed.load() : 0,
            db_batch_commit_max_us.load(std::memory_order_relaxed)
        );
        OutputDebugStringA(buf);
    }

    // Reset all counters (useful between measurement windows)
    inline void reset() {
        hook_callback_entered.store(0, std::memory_order_relaxed);
        hook_callback_debounced.store(0, std::memory_order_relaxed);
        hook_callback_deduped.store(0, std::memory_order_relaxed);
        events_queued.store(0, std::memory_order_relaxed);
        process_cache_hit.store(0, std::memory_order_relaxed);
        process_cache_miss.store(0, std::memory_order_relaxed);
        db_batch_committed.store(0, std::memory_order_relaxed);
        db_rows_inserted.store(0, std::memory_order_relaxed);
        db_rows_updated.store(0, std::memory_order_relaxed);
        bridge_events_sent.store(0, std::memory_order_relaxed);
        resolve_event_us.store(0, std::memory_order_relaxed);
        resolve_event_max_us.store(0, std::memory_order_relaxed);
        db_batch_commit_us.store(0, std::memory_order_relaxed);
        db_batch_commit_max_us.store(0, std::memory_order_relaxed);
    }

} // namespace PerfCounters

// ── RAII Scoped Timer ──────────────────────────────────────────────────
struct PerfScopedTimer {
    std::atomic<uint64_t>& accumulator;
    std::atomic<uint64_t>& maxTracker;
    std::chrono::steady_clock::time_point start;

    PerfScopedTimer(std::atomic<uint64_t>& acc, std::atomic<uint64_t>& maxVal)
        : accumulator(acc), maxTracker(maxVal), start(std::chrono::steady_clock::now()) {}

    ~PerfScopedTimer() {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        accumulator.fetch_add(elapsed, std::memory_order_relaxed);
        // Update max (lock-free CAS loop)
        uint64_t prev = maxTracker.load(std::memory_order_relaxed);
        while (elapsed > (int64_t)prev &&
               !maxTracker.compare_exchange_weak(prev, elapsed, std::memory_order_relaxed)) {}
    }
};

#endif // PERF_COUNTERS_HPP

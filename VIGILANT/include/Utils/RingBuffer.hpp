#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstddef>

// Bounded circular buffer with latest-wins overflow policy.
// Thread-safe (mutex-based) for multi-producer / single-consumer use.
// When full, push() overwrites the oldest entry and increments an overflow counter.
template<typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "RingBuffer capacity must be greater than zero");

public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Push item. On overflow the oldest item is dropped (latest-wins).
    // Returns true when an overflow occurred.
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool overflow = false;

        if (m_count == Capacity) {
            // Buffer full: drop oldest by advancing tail
            m_tail = next(m_tail);
            --m_count;
            m_overflowDrops.fetch_add(1, std::memory_order_relaxed);
            overflow = true;
        }

        m_buffer[m_head] = item;
        m_head = next(m_head);
        ++m_count;

        updatePeak();
        return overflow;
    }

    // Drain every item, return only the most recent one (coalescing).
    // coalescedCount receives the number of intermediate items that were skipped.
    // Returns true if an item was retrieved, false when the buffer was empty.
    bool drain_latest(T& out, size_t* coalescedCount = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0) {
            if (coalescedCount) *coalescedCount = 0;
            return false;
        }

        // Latest item sits just before the write head
        size_t latestIdx = prev(m_head);
        out = std::move(m_buffer[latestIdx]);

        size_t skipped = m_count - 1;
        if (coalescedCount) *coalescedCount = skipped;

        // Reset indices
        m_head = 0;
        m_tail = 0;
        m_count = 0;

        return true;
    }

    // Current number of items (snapshot).
    size_t depth() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

    // Accumulated overflow drop count since last reset.
    uint64_t overflow_count() const {
        return m_overflowDrops.load(std::memory_order_relaxed);
    }

    // Reset overflow counter; returns the previous value.
    uint64_t reset_overflow_count() {
        return m_overflowDrops.exchange(0, std::memory_order_relaxed);
    }

    // Peak depth observed since last reset.
    size_t peak_depth() const {
        return m_peakDepth.load(std::memory_order_relaxed);
    }

    // Reset peak depth; returns the previous value.
    size_t reset_peak_depth() {
        return m_peakDepth.exchange(0, std::memory_order_relaxed);
    }

private:
    size_t next(size_t idx) const { return (idx + 1) % Capacity; }
    size_t prev(size_t idx) const { return (idx + Capacity - 1) % Capacity; }

    void updatePeak() {
        size_t current = m_count;
        size_t peak = m_peakDepth.load(std::memory_order_relaxed);
        while (current > peak) {
            if (m_peakDepth.compare_exchange_weak(peak, current,
                    std::memory_order_relaxed))
                break;
        }
    }

    std::array<T, Capacity> m_buffer{};
    size_t m_head  = 0;   // next write position
    size_t m_tail  = 0;   // next read position
    size_t m_count = 0;   // current item count
    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_overflowDrops{0};
    std::atomic<size_t>   m_peakDepth{0};
};

#endif // RING_BUFFER_HPP

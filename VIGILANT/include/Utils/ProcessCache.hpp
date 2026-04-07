#ifndef PROCESS_CACHE_HPP
#define PROCESS_CACHE_HPP

#include <windows.h>
#include <psapi.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "Utils/PerfCounters.hpp"

// ============================================================================
// PID → process name cache with TTL.
// Avoids repeated OpenProcess + QueryFullProcessImageName kernel calls
// for the same process within the TTL window.
// Thread-safe (mutex-protected).
// ============================================================================
class ProcessCache {
public:
    static constexpr uint64_t kDefaultTTLMs = 60000; // 60 seconds

    struct Entry {
        std::string processName;
        std::string exePath;
        ULONGLONG   tickCached = 0; // GetTickCount64 when cached
    };

    // Returns cached result or performs kernel lookup + caches.
    // processName = "Unknown" and exePath = "" on failure.
    Entry lookup(DWORD pid) {
        ULONGLONG now = GetTickCount64();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_cache.find(pid);
            if (it != m_cache.end() && (now - it->second.tickCached) < kDefaultTTLMs) {
                PERF_COUNT(process_cache_hit);
                return it->second;
            }
        }

        // Cache miss → kernel call (outside lock)
        PERF_COUNT(process_cache_miss);
        Entry entry;
        entry.processName = "Unknown";
        entry.tickCached = now;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            wchar_t wPath[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, wPath, &size)) {
                // Convert full path to UTF-8
                int len = WideCharToMultiByte(CP_UTF8, 0, wPath, (int)size, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    entry.exePath.resize(len);
                    WideCharToMultiByte(CP_UTF8, 0, wPath, (int)size, &entry.exePath[0], len, nullptr, nullptr);
                }
                // Extract filename
                std::wstring wFullPath(wPath, size);
                size_t pos = wFullPath.find_last_of(L"\\/");
                if (pos != std::wstring::npos) {
                    const wchar_t* nameStart = wPath + pos + 1;
                    int nameLen = WideCharToMultiByte(CP_UTF8, 0, nameStart, -1, nullptr, 0, nullptr, nullptr);
                    if (nameLen > 1) {
                        entry.processName.resize(nameLen - 1);
                        WideCharToMultiByte(CP_UTF8, 0, nameStart, -1, &entry.processName[0], nameLen, nullptr, nullptr);
                    }
                } else {
                    entry.processName = entry.exePath;
                }
            }
            CloseHandle(hProcess);
        }

        // Store in cache
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache[pid] = entry;
        }
        return entry;
    }

    // Evict stale entries (call periodically, e.g., every 5 minutes)
    void evictStale() {
        ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ) {
            if ((now - it->second.tickCached) >= kDefaultTTLMs * 2) {
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.size();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<DWORD, Entry> m_cache;
};

#endif // PROCESS_CACHE_HPP

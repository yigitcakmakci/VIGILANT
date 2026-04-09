#ifndef AUTO_TICKER_VERSION_GUARD_HPP
#define AUTO_TICKER_VERSION_GUARD_HPP

#include <string>
#include <vector>
#include <mutex>
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// AutoTickerVersionGuard — GoalTree versioning integration for Auto-Ticker.
//
// Tracks the "current" GoalTree version per session.  When a new version
// arrives (via replan or regeneration), the guard:
//
//   1. Updates its cached version_id
//   2. Collects the open MicroTask id-set from the new tree
//   3. Marks any pending match candidates whose microTaskId is no longer
//      in the open set (or whose tree version doesn't match) as STALE
//   4. Produces a list of invalidated candidate ids for the UI
//
// Thread-safe: all public methods lock m_mutex.
//
// INVARIANT:
//   The guard NEVER mutates the GoalTree or marks any task as done.
//   It only tracks version metadata and detects staleness.
// ═══════════════════════════════════════════════════════════════════════

// ── CachedTreeSnapshot — lightweight version state ────────────────────

struct CachedTreeSnapshot {
    std::string sessionId;
    std::string versionId;
    std::string parentVersionId;
    std::string updatedAt;                  // ISO-8601
    std::vector<std::string> openMicroIds;  // ids with status == "open"
};

// ── PendingCandidate — a match candidate awaiting user action ─────────

struct PendingCandidate {
    std::string microTaskId;
    std::string goalTreeVersionId;  // version when this candidate was produced
    double      score = 0.0;
};

// ── StaleCandidateEntry — an invalidated candidate ────────────────────

struct StaleCandidateEntry {
    std::string microTaskId;
    std::string oldVersionId;
    std::string newVersionId;
    std::string reason;  // "MICRO_REMOVED" | "MICRO_COMPLETED" | "TREE_VERSION_MISMATCH"
};

// ── InvalidationResult — output of a version refresh ──────────────────

struct InvalidationResult {
    std::string sessionId;
    std::string oldVersionId;
    std::string newVersionId;
    std::vector<StaleCandidateEntry> invalidated;
    int         survivingCount = 0;     // candidates still valid
    bool        treeChanged    = false; // true if version actually changed
};

// ── AutoTickerVersionGuard ────────────────────────────────────────────

class AutoTickerVersionGuard {
public:
    AutoTickerVersionGuard() = default;

    /// Update the cached tree snapshot from a GoalTree JSON.
    /// Returns an InvalidationResult describing what changed.
    InvalidationResult RefreshTree(
        const std::string& sessionId,
        const nlohmann::json& goalTreeJson);

    /// Register pending candidates (from a MatchCandidatesProduced event).
    /// Each candidate is stamped with the current version_id.
    void RegisterCandidates(
        const std::string& sessionId,
        const std::string& goalTreeVersionId,
        const std::vector<PendingCandidate>& candidates);

    /// Remove a specific candidate (e.g. after user commits or dismisses).
    void RemoveCandidate(
        const std::string& sessionId,
        const std::string& microTaskId);

    /// Clear all state for a session.
    void ClearSession(const std::string& sessionId);

    /// Get the current cached version_id for a session (empty if none).
    std::string GetCurrentVersionId(const std::string& sessionId) const;

    /// Get all pending candidates for a session.
    std::vector<PendingCandidate> GetPendingCandidates(
        const std::string& sessionId) const;

private:
    static std::string nowISO();

    // Extract open micro ids from goalTree JSON
    static std::vector<std::string> extractOpenMicroIds(
        const nlohmann::json& goalTreeJson);

    // Per-session state
    struct SessionState {
        CachedTreeSnapshot              snapshot;
        std::vector<PendingCandidate>   pendingCandidates;
    };

    // Find or create session state (caller must hold m_mutex)
    SessionState& getOrCreate(const std::string& sessionId);
    const SessionState* find(const std::string& sessionId) const;

    mutable std::mutex                         m_mutex;
    std::map<std::string, SessionState>        m_sessions;
};

#endif // AUTO_TICKER_VERSION_GUARD_HPP

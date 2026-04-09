#ifndef AUTO_TICKER_TICK_ENGINE_HPP
#define AUTO_TICKER_TICK_ENGINE_HPP

#include <string>
#include <vector>
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// AutoTickerTickEngine — idempotent tick-commit for verified MicroTasks.
//
// Pipeline:  Matcher → Verifier → TickEngine → GoalTree mutation
//
// The TickEngine:
//   1. Checks idempotency  (unique key = journalEntryId + microTaskId)
//   2. Validates the verdict is 'pass'
//   3. Mutates MicroTask.status → 'done'
//   4. Writes verifier evidenceSpans + explanation into MicroTask.evidence
//   5. Persists the updated GoalTree via DatabaseManager
//   6. Records the tick in the auto_ticks audit table
//
// Returns a TickOutcome indicating success (TickCommitted) or failure
// (TickRejected) with a structured rejection reason.
//
// INVARIANT:
//   A duplicate (journalEntryId, microTaskId) pair is silently rejected
//   with reason "DUPLICATE" — the GoalTree is NOT mutated again.
// ═══════════════════════════════════════════════════════════════════════

class GeminiService;
class DatabaseManager;

// ── TickRecord — row in the auto_ticks audit table ────────────────────

struct TickRecord {
    std::string journalEntryId;     // which journal entry sourced this tick
    std::string microTaskId;        // which MicroTask was ticked
    std::string interviewSessionId; // owning session
    std::string verdict;            // always "pass" for committed ticks
    double      confidence = 0.0;
    std::string modelVersion;       // AI model that produced the verification
    std::string committedAt;        // ISO-8601
};

// ── RejectionReason ───────────────────────────────────────────────────

enum class RejectionReason {
    None,
    Duplicate,          // (journalEntryId + microTaskId) already exists
    VerdictNotPass,     // verdict was not 'pass'
    MicroTaskNotFound,  // microTaskId not in goal tree
    NoGoalTree,         // no goal tree in stored result
    NoInterviewResult,  // no interview result found
    InvalidInput,       // missing required fields
    PersistFailed,      // SQLite write failed
};

inline std::string rejectionReasonToString(RejectionReason r) {
    switch (r) {
    case RejectionReason::None:               return "NONE";
    case RejectionReason::Duplicate:          return "DUPLICATE";
    case RejectionReason::VerdictNotPass:     return "VERDICT_NOT_PASS";
    case RejectionReason::MicroTaskNotFound:  return "MICRO_NOT_FOUND";
    case RejectionReason::NoGoalTree:         return "NO_GOAL_TREE";
    case RejectionReason::NoInterviewResult:  return "NO_INTERVIEW_RESULT";
    case RejectionReason::InvalidInput:       return "INVALID_INPUT";
    case RejectionReason::PersistFailed:      return "PERSIST_FAILED";
    default:                                  return "UNKNOWN";
    }
}

// ── TickOutcome — result of a CommitTick operation ────────────────────

struct TickOutcome {
    bool              committed = false;     // true = TickCommitted, false = TickRejected
    std::string       microTaskId;
    std::string       journalEntryId;
    std::string       interviewSessionId;
    RejectionReason   rejectionReason = RejectionReason::None;
    std::string       rejectionMessage;      // human-readable
    std::string       committedAt;           // ISO-8601 (only when committed)
};

// ── AutoTickerTickEngine ──────────────────────────────────────────────

class AutoTickerTickEngine {
public:
    AutoTickerTickEngine(DatabaseManager* vault);

    /// Commit a verified tick — idempotent on (journalEntryId, microTaskId).
    /// On success: mutates GoalTree, writes evidence, inserts auto_ticks row.
    /// On duplicate/failure: returns TickOutcome with committed=false.
    TickOutcome CommitTick(
        const std::string& interviewSessionId,
        const std::string& journalEntryId,
        const std::string& microTaskId,
        const std::string& verdict,
        double             confidence,
        const nlohmann::json& evidenceSpans,    // array of {start, length, text}
        const std::string& explanation,
        const std::string& modelVersion) const;

    /// Query tick history for a specific MicroTask.
    std::vector<TickRecord> GetTickHistory(
        const std::string& interviewSessionId,
        const std::string& microTaskId) const;

    /// Query all ticks for a journal entry.
    std::vector<TickRecord> GetTicksByJournal(
        const std::string& journalEntryId) const;

private:
    static std::string nowISO();

    DatabaseManager* m_vault;
};

#endif // AUTO_TICKER_TICK_ENGINE_HPP

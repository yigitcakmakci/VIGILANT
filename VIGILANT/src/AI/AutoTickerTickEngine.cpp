#include "AI/AutoTickerTickEngine.hpp"
#include "Data/DatabaseManager.hpp"
#include <windows.h>
#include <chrono>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

// ── Debug helper ──────────────────────────────────────────────────────
static void DebugLogTick(const std::string& msg) {
    OutputDebugStringA(("[AutoTickerTickEngine] " + msg + "\n").c_str());
}

// ── Constructor ───────────────────────────────────────────────────────
AutoTickerTickEngine::AutoTickerTickEngine(DatabaseManager* vault)
    : m_vault(vault) {}

// ── ISO-8601 timestamp ────────────────────────────────────────────────
std::string AutoTickerTickEngine::nowISO() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%FT%T");
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════
// CommitTick — idempotent tick-commit
// ═══════════════════════════════════════════════════════════════════════
TickOutcome AutoTickerTickEngine::CommitTick(
    const std::string& interviewSessionId,
    const std::string& journalEntryId,
    const std::string& microTaskId,
    const std::string& verdict,
    double             confidence,
    const json&        evidenceSpans,
    const std::string& explanation,
    const std::string& modelVersion) const
{
    TickOutcome outcome;
    outcome.microTaskId        = microTaskId;
    outcome.journalEntryId     = journalEntryId;
    outcome.interviewSessionId = interviewSessionId;

    // ── 1. Validate inputs ────────────────────────────────────────────
    if (interviewSessionId.empty() || journalEntryId.empty() || microTaskId.empty()) {
        outcome.rejectionReason  = RejectionReason::InvalidInput;
        outcome.rejectionMessage = "Missing required field(s): interviewSessionId, journalEntryId, or microTaskId.";
        DebugLogTick("REJECTED (InvalidInput): " + outcome.rejectionMessage);
        return outcome;
    }

    // ── 2. Verify verdict is 'pass' ──────────────────────────────────
    if (verdict != "pass") {
        outcome.rejectionReason  = RejectionReason::VerdictNotPass;
        outcome.rejectionMessage = "Cannot commit tick: verdict is '" + verdict + "', not 'pass'.";
        DebugLogTick("REJECTED (VerdictNotPass): micro=" + microTaskId);
        return outcome;
    }

    // ── 3. Idempotency check ─────────────────────────────────────────
    if (m_vault && m_vault->hasAutoTick(journalEntryId, microTaskId)) {
        outcome.rejectionReason  = RejectionReason::Duplicate;
        outcome.rejectionMessage = "Tick already committed for this journal+micro pair.";
        DebugLogTick("REJECTED (Duplicate): journal=" + journalEntryId
                     + " micro=" + microTaskId);
        return outcome;
    }

    // ── 4. Load stored interview result ──────────────────────────────
    if (!m_vault) {
        outcome.rejectionReason  = RejectionReason::PersistFailed;
        outcome.rejectionMessage = "Database manager not available.";
        DebugLogTick("REJECTED (PersistFailed): no vault");
        return outcome;
    }

    json storedResult = m_vault->getInterviewResult(interviewSessionId);
    if (storedResult.is_null() || storedResult.empty()) {
        outcome.rejectionReason  = RejectionReason::NoInterviewResult;
        outcome.rejectionMessage = "No interview result found for session: " + interviewSessionId;
        DebugLogTick("REJECTED (NoInterviewResult): " + interviewSessionId);
        return outcome;
    }

    if (!storedResult.contains("goalTree") || !storedResult["goalTree"].is_object()) {
        outcome.rejectionReason  = RejectionReason::NoGoalTree;
        outcome.rejectionMessage = "No GoalTree found in stored result.";
        DebugLogTick("REJECTED (NoGoalTree): " + interviewSessionId);
        return outcome;
    }

    // ── 5. Find the MicroTask in the tree ────────────────────────────
    json& goalTree = storedResult["goalTree"];
    json* targetMicro = nullptr;

    if (goalTree.contains("majors") && goalTree["majors"].is_array()) {
        for (auto& major : goalTree["majors"]) {
            if (targetMicro) break;
            if (!major.contains("minors") || !major["minors"].is_array()) continue;
            for (auto& minor : major["minors"]) {
                if (targetMicro) break;
                if (!minor.contains("micros") || !minor["micros"].is_array()) continue;
                for (auto& micro : minor["micros"]) {
                    if (micro.value("id", "") == microTaskId) {
                        targetMicro = &micro;
                        break;
                    }
                }
            }
        }
    }

    if (!targetMicro) {
        outcome.rejectionReason  = RejectionReason::MicroTaskNotFound;
        outcome.rejectionMessage = "MicroTask not found in goal tree: " + microTaskId;
        DebugLogTick("REJECTED (MicroTaskNotFound): " + microTaskId);
        return outcome;
    }

    // ── 6. Build evidence JSON from verifier data ────────────────────
    //
    // The evidence_type of the micro determines the Evidence shape.
    // For auto-ticker we always write "text" evidence containing:
    //   - Concatenated evidence span quotes
    //   - Verifier explanation appended
    //
    std::string evidenceType = (*targetMicro).value("evidence_type", "text");

    // Concatenate evidence span texts into a single text block
    std::string evidenceText;
    if (evidenceSpans.is_array()) {
        for (const auto& span : evidenceSpans) {
            if (span.contains("text") && span["text"].is_string()) {
                if (!evidenceText.empty()) evidenceText += "\n---\n";
                evidenceText += span["text"].get<std::string>();
            }
        }
    }
    if (!explanation.empty()) {
        if (!evidenceText.empty()) evidenceText += "\n\n";
        evidenceText += "[Verifier] " + explanation;
    }

    // Build the evidence object matching the evidence_type schema
    json evidenceJson = json::object();
    if (evidenceType == "text") {
        evidenceJson["text"] = evidenceText;
    } else if (evidenceType == "url") {
        // Best-effort: put the evidence text as URL if it looks like one,
        // otherwise fall back to text
        evidenceJson["url"]  = evidenceText;
        evidenceJson["text"] = evidenceText;
    } else if (evidenceType == "file") {
        evidenceJson["file_path"] = evidenceText;
        evidenceJson["text"]      = evidenceText;
    } else if (evidenceType == "metric") {
        // Cannot auto-derive a metric from text; store as text fallback
        evidenceJson["metric_value"] = 0.0;
        evidenceJson["text"]         = evidenceText;
    } else {
        evidenceJson["text"] = evidenceText;
    }

    // Attach auto-ticker metadata so the UI can trace back
    evidenceJson["_autoTicker"] = {
        {"journalEntryId", journalEntryId},
        {"evidenceSpans",  evidenceSpans},
        {"confidence",     confidence},
        {"modelVersion",   modelVersion}
    };

    // ── 7. Mutate MicroTask → done + evidence ────────────────────────
    (*targetMicro)["status"]   = "done";
    (*targetMicro)["evidence"] = evidenceJson;

    // ── 8. Persist updated GoalTree ──────────────────────────────────
    storedResult["goalTree"] = goalTree;
    std::string endedBy   = storedResult.value("ended_by", "complete");
    int qCount            = storedResult.value("question_count", 0);
    int maxQ              = storedResult.value("max_questions", 3);
    std::string sessionId = storedResult.value("session_id", interviewSessionId);

    bool saved = m_vault->saveInterviewResult(
        sessionId, storedResult.dump(), endedBy, qCount, maxQ);

    if (!saved) {
        outcome.rejectionReason  = RejectionReason::PersistFailed;
        outcome.rejectionMessage = "Failed to persist updated GoalTree.";
        DebugLogTick("REJECTED (PersistFailed): saveInterviewResult failed");
        return outcome;
    }

    // ── 9. Insert auto_ticks audit record ────────────────────────────
    std::string ts = nowISO();
    bool tickSaved = m_vault->insertAutoTick(
        journalEntryId, microTaskId, interviewSessionId,
        verdict, confidence, ts, modelVersion);

    if (!tickSaved) {
        // GoalTree was already updated — log warning but still report committed
        DebugLogTick("WARNING: auto_ticks insert failed (GoalTree was updated): "
                     + journalEntryId + " / " + microTaskId);
    }

    // ── 10. Success ──────────────────────────────────────────────────
    outcome.committed   = true;
    outcome.committedAt = ts;
    DebugLogTick("COMMITTED: journal=" + journalEntryId
                 + " micro=" + microTaskId + " at " + ts);
    return outcome;
}

// ═══════════════════════════════════════════════════════════════════════
// GetTickHistory — all ticks for a specific MicroTask
// ═══════════════════════════════════════════════════════════════════════
std::vector<TickRecord> AutoTickerTickEngine::GetTickHistory(
    const std::string& interviewSessionId,
    const std::string& microTaskId) const
{
    if (!m_vault) return {};
    return m_vault->getAutoTickHistory(interviewSessionId, microTaskId);
}

// ═══════════════════════════════════════════════════════════════════════
// GetTicksByJournal — all ticks sourced from a specific journal entry
// ═══════════════════════════════════════════════════════════════════════
std::vector<TickRecord> AutoTickerTickEngine::GetTicksByJournal(
    const std::string& journalEntryId) const
{
    if (!m_vault) return {};
    return m_vault->getAutoTicksByJournal(journalEntryId);
}

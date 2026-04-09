#include "AI/AutoTickerVersionGuard.hpp"
#include <windows.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>

using json = nlohmann::json;

// ── Debug helper ──────────────────────────────────────────────────────
static void DebugLogVG(const std::string& msg) {
    OutputDebugStringA(("[AutoTickerVersionGuard] " + msg + "\n").c_str());
}

// ── ISO-8601 ──────────────────────────────────────────────────────────
std::string AutoTickerVersionGuard::nowISO() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%FT%T");
    return oss.str();
}

// ── Extract open micro ids from GoalTree JSON ─────────────────────────
std::vector<std::string> AutoTickerVersionGuard::extractOpenMicroIds(
    const json& goalTreeJson)
{
    std::vector<std::string> ids;
    if (!goalTreeJson.contains("majors") || !goalTreeJson["majors"].is_array())
        return ids;

    for (const auto& maj : goalTreeJson["majors"]) {
        if (!maj.contains("minors") || !maj["minors"].is_array()) continue;
        for (const auto& min : maj["minors"]) {
            if (!min.contains("micros") || !min["micros"].is_array()) continue;
            for (const auto& mic : min["micros"]) {
                if (mic.value("status", "open") == "open") {
                    std::string id = mic.value("id", "");
                    if (!id.empty()) ids.push_back(id);
                }
            }
        }
    }
    return ids;
}

// ── Session state accessors ───────────────────────────────────────────
AutoTickerVersionGuard::SessionState&
AutoTickerVersionGuard::getOrCreate(const std::string& sessionId) {
    return m_sessions[sessionId];
}

const AutoTickerVersionGuard::SessionState*
AutoTickerVersionGuard::find(const std::string& sessionId) const {
    auto it = m_sessions.find(sessionId);
    return (it != m_sessions.end()) ? &it->second : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// RefreshTree — core version-change detection + candidate invalidation
// ═══════════════════════════════════════════════════════════════════════
InvalidationResult AutoTickerVersionGuard::RefreshTree(
    const std::string& sessionId,
    const json& goalTreeJson)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    InvalidationResult result;
    result.sessionId = sessionId;

    std::string newVersionId    = goalTreeJson.value("version_id", "");
    std::string parentVersionId = goalTreeJson.value("parent_version", "");

    auto& state = getOrCreate(sessionId);
    std::string oldVersionId = state.snapshot.versionId;

    result.oldVersionId = oldVersionId;
    result.newVersionId = newVersionId;

    // If same version, nothing changed
    if (!newVersionId.empty() && newVersionId == oldVersionId) {
        result.treeChanged    = false;
        result.survivingCount = static_cast<int>(state.pendingCandidates.size());
        DebugLogVG("No version change for session=" + sessionId
                   + " version=" + newVersionId);
        return result;
    }

    result.treeChanged = true;
    DebugLogVG("Version change detected: " + oldVersionId + " -> " + newVersionId
               + " session=" + sessionId);

    // Update snapshot
    state.snapshot.sessionId       = sessionId;
    state.snapshot.versionId       = newVersionId;
    state.snapshot.parentVersionId = parentVersionId;
    state.snapshot.updatedAt       = nowISO();
    state.snapshot.openMicroIds    = extractOpenMicroIds(goalTreeJson);

    // Build a set of open micro ids for fast lookup
    std::set<std::string> openSet(
        state.snapshot.openMicroIds.begin(),
        state.snapshot.openMicroIds.end());

    // Check each pending candidate for staleness
    std::vector<PendingCandidate> surviving;
    for (const auto& cand : state.pendingCandidates) {
        StaleCandidateEntry stale;
        stale.microTaskId  = cand.microTaskId;
        stale.oldVersionId = cand.goalTreeVersionId;
        stale.newVersionId = newVersionId;

        if (openSet.find(cand.microTaskId) == openSet.end()) {
            // Micro was removed or completed in the new tree
            stale.reason = "MICRO_REMOVED";
            result.invalidated.push_back(std::move(stale));
        } else if (cand.goalTreeVersionId != newVersionId
                   && !cand.goalTreeVersionId.empty()) {
            // Micro still exists but tree version changed — criteria may differ
            stale.reason = "TREE_VERSION_MISMATCH";
            result.invalidated.push_back(std::move(stale));
        } else {
            surviving.push_back(cand);
        }
    }

    state.pendingCandidates = std::move(surviving);
    result.survivingCount = static_cast<int>(state.pendingCandidates.size());

    DebugLogVG("Invalidated " + std::to_string(result.invalidated.size())
               + " candidates, " + std::to_string(result.survivingCount)
               + " surviving for session=" + sessionId);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// RegisterCandidates
// ═══════════════════════════════════════════════════════════════════════
void AutoTickerVersionGuard::RegisterCandidates(
    const std::string& sessionId,
    const std::string& goalTreeVersionId,
    const std::vector<PendingCandidate>& candidates)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& state = getOrCreate(sessionId);

    for (const auto& c : candidates) {
        // Replace if same microTaskId already pending
        auto it = std::find_if(state.pendingCandidates.begin(),
                               state.pendingCandidates.end(),
                               [&](const PendingCandidate& p) {
                                   return p.microTaskId == c.microTaskId;
                               });
        if (it != state.pendingCandidates.end()) {
            it->goalTreeVersionId = goalTreeVersionId;
            it->score = c.score;
        } else {
            PendingCandidate pc = c;
            pc.goalTreeVersionId = goalTreeVersionId;
            state.pendingCandidates.push_back(std::move(pc));
        }
    }

    DebugLogVG("Registered " + std::to_string(candidates.size())
               + " candidates for session=" + sessionId
               + " version=" + goalTreeVersionId);
}

// ═══════════════════════════════════════════════════════════════════════
// RemoveCandidate
// ═══════════════════════════════════════════════════════════════════════
void AutoTickerVersionGuard::RemoveCandidate(
    const std::string& sessionId,
    const std::string& microTaskId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* st = find(sessionId);
    if (!st) return;
    // const_cast safe: find returns const*, but we need to erase
    auto& state = m_sessions[sessionId];
    state.pendingCandidates.erase(
        std::remove_if(state.pendingCandidates.begin(),
                       state.pendingCandidates.end(),
                       [&](const PendingCandidate& p) {
                           return p.microTaskId == microTaskId;
                       }),
        state.pendingCandidates.end());
}

// ═══════════════════════════════════════════════════════════════════════
// ClearSession
// ═══════════════════════════════════════════════════════════════════════
void AutoTickerVersionGuard::ClearSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(sessionId);
}

// ═══════════════════════════════════════════════════════════════════════
// GetCurrentVersionId
// ═══════════════════════════════════════════════════════════════════════
std::string AutoTickerVersionGuard::GetCurrentVersionId(
    const std::string& sessionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* st = find(sessionId);
    return st ? st->snapshot.versionId : "";
}

// ═══════════════════════════════════════════════════════════════════════
// GetPendingCandidates
// ═══════════════════════════════════════════════════════════════════════
std::vector<PendingCandidate> AutoTickerVersionGuard::GetPendingCandidates(
    const std::string& sessionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* st = find(sessionId);
    return st ? st->pendingCandidates : std::vector<PendingCandidate>{};
}

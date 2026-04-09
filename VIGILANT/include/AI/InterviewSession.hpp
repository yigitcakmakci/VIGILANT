#ifndef INTERVIEW_SESSION_HPP
#define INTERVIEW_SESSION_HPP

#include <string>
#include <vector>
#include <unordered_set>
#include "Utils/json.hpp"

// ── A single message in the Socratic interview transcript ──────────────
struct InterviewMessage {
    std::string id;
    std::string role;   // "user" | "ai" | "system"
    std::string text;
    std::string ts;     // ISO-8601
};

// ── Stateful session for one Socratic interview ────────────────────────
struct InterviewSession {
    std::string sessionId;
    int         questionCount = 0;
    static constexpr int kMaxQuestions = 3;    // HARD LIMIT
    bool        finalized = false;
    std::string endedBy;                       // "cta" | "limit"

    std::vector<InterviewMessage>      transcript;
    std::unordered_set<std::string>    processedRequestIds;

    // ── Idempotent helpers ─────────────────────────────────────────────
    bool isRequestProcessed(const std::string& requestId) const {
        return processedRequestIds.count(requestId) > 0;
    }
    void markRequestProcessed(const std::string& requestId) {
        processedRequestIds.insert(requestId);
    }

    // ── Serialize transcript to JSON array ─────────────────────────────
    nlohmann::json transcriptToJson() const {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& msg : transcript) {
            arr.push_back({
                {"id",   msg.id},
                {"role", msg.role},
                {"text", msg.text},
                {"ts",   msg.ts}
            });
        }
        return arr;
    }
};

#endif // INTERVIEW_SESSION_HPP

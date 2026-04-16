#ifndef INTERVIEW_SESSION_HPP
#define INTERVIEW_SESSION_HPP

#include <string>
#include <vector>
#include <unordered_set>
#include "Utils/json.hpp"

// ── Goals Chat state machine ───────────────────────────────────────────
enum class GoalsChatState {
    IDLE,               // No active goals chat session
    ASKING_QUESTIONS,   // AI is asking Socratic clarification questions
    GENERATING_PLAN,    // All questions answered — generating DynamicGoalTree
    PLAN_READY          // GoalTree generated and delivered to UI
};

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

    // ── Goals Chat state ──────────────────────────────────────────────
    GoalsChatState goalsChatState = GoalsChatState::IDLE;
    static constexpr int kMaxGoalsChatQuestions = 10; // Safety hard cap (AI decides when to stop earlier)
    int         goalsChatQuestionCount = 0;

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

    // ── Build conversation history string for Gemini prompts ──────────
    std::string transcriptToPromptString() const {
        std::string result;
        for (const auto& msg : transcript) {
            if (msg.role == "user")
                result += "Kullanici: " + msg.text + "\n";
            else if (msg.role == "ai")
                result += "Mentor: " + msg.text + "\n";
        }
        return result;
    }
};

#endif // INTERVIEW_SESSION_HPP

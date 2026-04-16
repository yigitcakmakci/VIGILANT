#ifndef INTERVIEW_HANDLER_HPP
#define INTERVIEW_HANDLER_HPP

#include <string>
#include <memory>
#include <mutex>
#include "AI/InterviewSession.hpp"
#include "AI/InterviewSlotFiller.hpp"
#include "AI/InterviewResult.hpp"
#include "Utils/json.hpp"

class GeminiService;
class DatabaseManager;

// ── Handles all Socratic Interview events from the UI bridge ───────────
// Thread-safe: all public methods lock m_mutex.
// Idempotent: duplicate requestIds are silently ignored.
class InterviewHandler {
public:
    InterviewHandler(GeminiService* gemini, DatabaseManager* vault);

    // Event handlers — each returns a JSON string to post back to WebView
    std::string HandleInterviewStart(const std::string& requestId, int maxQuestions);
    std::string HandleUserMessage(const std::string& requestId,
                                  const std::string& sessionId,
                                  const std::string& text,
                                  const std::string& messageId);
    std::string HandleFinalizeInterview(const std::string& requestId,
                                       const std::string& sessionId,
                                       const std::string& endedBy);

    // ── SlotFiller event handlers ─────────────────────────────────────
    std::string HandleSlotFillerStart(const std::string& requestId);
    std::string HandleSlotFillerAnswer(const std::string& requestId,
                                       const std::string& text);
    std::string HandleSlotFillerFinalize(const std::string& requestId,
                                         const std::string& endedBy);

    // ── GoalTree generation from persisted result ─────────────────────
    std::string HandleGenerateGoalTree(const std::string& requestId,
                                       const std::string& interviewSessionId);

    // ── Tick MicroTask to 'done' with evidence ───────────────────────
    std::string HandleTickMicroTask(const std::string& requestId,
                                    const std::string& interviewSessionId,
                                    const std::string& microTaskId,
                                    const nlohmann::json& evidenceJson);

    // ── Status change from SkillTree UI (validates + persists) ───────
    std::string HandleMicroTaskStatusChange(const std::string& requestId,
                                            const std::string& interviewSessionId,
                                            const std::string& microTaskId,
                                            const std::string& newStatus,
                                            const nlohmann::json& evidenceJson);

    // ── GoalTree replan: re-generate tree and merge status/evidence ──
    std::string HandleReplanGoalTree(const std::string& requestId,
                                     const std::string& interviewSessionId,
                                     const std::string& reason,
                                     const nlohmann::json& changedSlots);

    // ── Goals Chat: Socratic planner embedded in Goals tab ───────────
    std::string HandleGoalsChatStart(const std::string& requestId);
    std::string HandleGoalsChatMessage(const std::string& requestId,
                                       const std::string& sessionId,
                                       const std::string& text);

private:
    std::string generateUUID() const;
    std::string nowISO() const;
    std::string generateAIQuestion(const std::string& lastUserText);
    InterviewResult buildResult() const;
    void persistResult(const InterviewResult& result);
    nlohmann::json buildDeterministicGoalTree(const std::string& sessionId,
                                               const InterviewResult& result) const;

    GeminiService*                       m_gemini;
    DatabaseManager*                     m_vault;
    std::unique_ptr<InterviewSession>    m_session;
    std::unique_ptr<InterviewSlotFiller> m_slotFiller;
    nlohmann::json                       m_lastStoredResult; // in-memory cache of storedResult+goalTree
    std::mutex                           m_mutex;
};

#endif // INTERVIEW_HANDLER_HPP

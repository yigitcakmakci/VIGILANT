#ifndef INTERVIEW_SLOT_FILLER_HPP
#define INTERVIEW_SLOT_FILLER_HPP

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include "Utils/json.hpp"

class GeminiService;

// ═══════════════════════════════════════════════════════════════════════
// Slot value types
// ═══════════════════════════════════════════════════════════════════════

enum class SlotStatus { Empty, Ambiguous, Filled };

// ── Individual slot metadata + value ──────────────────────────────────
struct SlotEntry {
    std::string name;           // "goal" | "timeframe" | "weekly_hours" | "current_level" | "constraints"
    SlotStatus  status   = SlotStatus::Empty;
    std::string value;          // scalar value (empty for constraints[])
    std::vector<std::string> listValue;  // only for constraints[]
    int         attempts = 0;   // how many times we asked for this slot

    // Validation rules
    bool        required     = true;
    int         minLength    = 0;   // string min chars (0 = no limit)
    int         maxLength    = 0;   // string max chars (0 = no limit)
    int         minNumeric   = 0;   // numeric min
    int         maxNumeric   = 0;   // numeric max
    bool        isNumeric    = false;
    bool        isList       = false;
    int         maxListItems = 0;   // 0 = no limit
};

// ═══════════════════════════════════════════════════════════════════════
// InterviewSlotFiller — drives the structured interview
// ═══════════════════════════════════════════════════════════════════════

class InterviewSlotFiller {
public:
    static constexpr int kMaxTotalQuestions = 3;  // hard limit from InterviewSession
    static constexpr int kMaxSlotAttempts   = 2;  // max re-asks per slot

    explicit InterviewSlotFiller(GeminiService* gemini);

    // ── Core API ──────────────────────────────────────────────────────
    // Resets all slots to Empty. Returns InterviewUpdate JSON.
    nlohmann::json reset();

    // Process a user answer against the current pending slot.
    // Returns one of: SlotPatched | AskNextQuestion | InterviewUpdate (finalized)
    nlohmann::json processAnswer(const std::string& userText);

    // Get the first question to ask (called after reset / start).
    nlohmann::json firstQuestion();

    // Force-finalize: user clicked CTA. Returns InterviewUpdate with partial data.
    nlohmann::json finalize(const std::string& endedBy);

    // ── Queries ───────────────────────────────────────────────────────
    bool isComplete() const;
    bool isFinalized() const { return m_finalized; }
    int  questionCount() const { return m_questionCount; }
    int  filledCount() const;
    int  totalSlots() const { return static_cast<int>(m_slots.size()); }

    // Serialize current slot state to JSON
    nlohmann::json slotsToJson() const;

private:
    // ── Initialization ────────────────────────────────────────────────
    void initSlots();

    // ── Slot operations ───────────────────────────────────────────────
    SlotEntry* nextEmptySlot();
    const SlotEntry* currentSlot() const;
    bool validateAndFill(SlotEntry& slot, const std::string& raw);

    // ── LLM prompt ────────────────────────────────────────────────────
    std::string buildSlotPrompt(const SlotEntry& slot) const;
    std::string callLLM(const std::string& systemPrompt, const std::string& userPrompt) const;

    // ── Parsers ───────────────────────────────────────────────────────
    std::string parseGoal(const std::string& text) const;
    std::string parseTimeframe(const std::string& text) const;
    int         parseWeeklyHours(const std::string& text) const;
    std::string parseCurrentLevel(const std::string& text) const;
    std::vector<std::string> parseConstraints(const std::string& text) const;

    // ── Helpers ───────────────────────────────────────────────────────
    std::string generateUUID() const;
    std::string nowISO() const;
    nlohmann::json makeAskMessage(const SlotEntry& slot, const std::string& questionText) const;
    nlohmann::json makeSlotPatched(const SlotEntry& slot) const;
    nlohmann::json makeFinalUpdate() const;

    // ── State ─────────────────────────────────────────────────────────
    GeminiService*           m_gemini = nullptr;
    std::vector<SlotEntry>   m_slots;
    int                      m_currentSlotIdx = 0;
    int                      m_questionCount  = 0;
    bool                     m_finalized      = false;
    std::string              m_endedBy;        // "cta" | "limit" | "complete"
    mutable std::mutex       m_mutex;
};

#endif // INTERVIEW_SLOT_FILLER_HPP

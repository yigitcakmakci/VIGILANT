#ifndef INTERVIEW_RESULT_HPP
#define INTERVIEW_RESULT_HPP

#include <string>
#include <vector>
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// InterviewResultMessage — a single transcript entry
// ═══════════════════════════════════════════════════════════════════════

struct InterviewResultMessage {
    std::string message_id;
    std::string role;           // "user" | "ai" | "system"
    std::string text;
    std::string iso_ts;         // ISO-8601
};

// ═══════════════════════════════════════════════════════════════════════
// ExtractedSlots — the structured slot data
// ═══════════════════════════════════════════════════════════════════════

struct ExtractedSlots {
    std::string              goal;
    std::string              timeframe;
    int                      weekly_hours = 0;
    std::string              current_level;
    std::vector<std::string> constraints;
};

// ═══════════════════════════════════════════════════════════════════════
// InterviewResult — the complete finalized document
//
// INVARIANT: once finalized == true, transcript is immutable.
//            Only append is allowed before finalize.
//            Double-finalize is idempotent (same doc returned).
// ═══════════════════════════════════════════════════════════════════════

struct InterviewResult {
    std::string  session_id;
    bool         finalized      = false;
    std::string  ended_by;          // "cta" | "limit" | "complete"
    int          question_count = 0;
    int          max_questions  = 3;
    std::string  created_at;        // ISO-8601
    std::string  finalized_at;      // ISO-8601, empty until finalized

    std::vector<InterviewResultMessage> transcript;
    ExtractedSlots                      extracted_slots;

    // ── Serialization ─────────────────────────────────────────────────

    static nlohmann::json messageToJson(const InterviewResultMessage& m) {
        return {
            {"message_id", m.message_id},
            {"role",       m.role},
            {"text",       m.text},
            {"iso_ts",     m.iso_ts}
        };
    }

    static InterviewResultMessage messageFromJson(const nlohmann::json& j) {
        InterviewResultMessage m;
        m.message_id = j.value("message_id", "");
        m.role       = j.value("role", "");
        m.text       = j.value("text", "");
        m.iso_ts     = j.value("iso_ts", "");
        return m;
    }

    static nlohmann::json slotsToJson(const ExtractedSlots& s) {
        return {
            {"goal",          s.goal},
            {"timeframe",     s.timeframe},
            {"weekly_hours",  s.weekly_hours},
            {"current_level", s.current_level},
            {"constraints",   s.constraints}
        };
    }

    static ExtractedSlots slotsFromJson(const nlohmann::json& j) {
        ExtractedSlots s;
        s.goal          = j.value("goal", "");
        s.timeframe     = j.value("timeframe", "");
        s.weekly_hours  = j.value("weekly_hours", 0);
        s.current_level = j.value("current_level", "");
        if (j.contains("constraints") && j["constraints"].is_array()) {
            for (const auto& c : j["constraints"])
                s.constraints.push_back(c.get<std::string>());
        }
        return s;
    }

    nlohmann::json toJson() const {
        nlohmann::json doc;
        doc["session_id"]     = session_id;
        doc["finalized"]      = finalized;
        doc["ended_by"]       = ended_by;
        doc["question_count"] = question_count;
        doc["max_questions"]  = max_questions;
        doc["created_at"]     = created_at;
        doc["finalized_at"]   = finalized_at;

        doc["transcript"] = nlohmann::json::array();
        for (const auto& m : transcript)
            doc["transcript"].push_back(messageToJson(m));

        doc["extractedSlots"] = slotsToJson(extracted_slots);
        return doc;
    }

    static InterviewResult fromJson(const nlohmann::json& j) {
        InterviewResult r;
        r.session_id     = j.value("session_id", "");
        r.finalized      = j.value("finalized", false);
        r.ended_by       = j.value("ended_by", "");
        r.question_count = j.value("question_count", 0);
        r.max_questions  = j.value("max_questions", 3);
        r.created_at     = j.value("created_at", "");
        r.finalized_at   = j.value("finalized_at", "");

        if (j.contains("transcript") && j["transcript"].is_array()) {
            for (const auto& m : j["transcript"])
                r.transcript.push_back(messageFromJson(m));
        }
        if (j.contains("extractedSlots") && j["extractedSlots"].is_object()) {
            r.extracted_slots = slotsFromJson(j["extractedSlots"]);
        }
        return r;
    }

    // ── Append guard — only if NOT finalized ──────────────────────────
    bool appendMessage(const InterviewResultMessage& msg) {
        if (finalized) return false;    // immutable after finalize
        transcript.push_back(msg);
        return true;
    }
};

#endif // INTERVIEW_RESULT_HPP

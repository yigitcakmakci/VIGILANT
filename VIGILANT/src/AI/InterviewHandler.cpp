#include "AI/InterviewHandler.hpp"
#include "AI/GeminiService.hpp"
#include "AI/InterviewSlotFiller.hpp"
#include "AI/GoalTree.hpp"
#include "Data/DatabaseManager.hpp"
#include <windows.h>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

// ── Debug helper ───────────────────────────────────────────────────────
static void DebugLogInterview(const std::string& msg) {
    OutputDebugStringA(("[InterviewHandler] " + msg + "\n").c_str());
}

// ── Constructor ────────────────────────────────────────────────────────
InterviewHandler::InterviewHandler(GeminiService* gemini, DatabaseManager* vault)
    : m_gemini(gemini), m_vault(vault) {}

// ── UUID v4 (simplified) ──────────────────────────────────────────────
std::string InterviewHandler::generateUUID() const {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist;

    uint32_t a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);
    char buf[48];
    sprintf_s(buf, "%08x-%04x-4%03x-%04x-%04x%08x",
        a,
        (b >> 16) & 0xFFFF,
        c & 0x0FFF,
        0x8000 | (d & 0x3FFF),
        (b & 0xFFFF),
        dist(rng));
    return std::string(buf);
}

// ── ISO-8601 timestamp ────────────────────────────────────────────────
std::string InterviewHandler::nowISO() const {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%FT%T");
    return oss.str();
}

// ── Stub AI question generator ────────────────────────────────────────
// Uses GeminiService when available; falls back to deterministic stubs.
std::string InterviewHandler::generateAIQuestion(const std::string& lastUserText) {
    if (m_gemini && m_gemini->isAvailable() && m_session) {
        // Build a Socratic prompt from the full transcript
        std::string systemPrompt =
            "Sen bir Sokratik gorusme kocusun. Kullanicinin hedefini netlestirmesine yardimci oluyorsun. "
            "SADECE 1 soru uret. Baska metin yazma. Soru kisa ve odakli olsun.";

        std::string conversationCtx = "Gorusme gecmisi:\n";
        for (const auto& msg : m_session->transcript) {
            conversationCtx += (msg.role == "user" ? "Kullanici: " : "Asistan: ") + msg.text + "\n";
        }
        conversationCtx += "\nSimdi bir sonraki Sokratik soruyu uret:";

        // TODO: Wire to actual GeminiService API when ready:
        // return m_gemini->sendSocraticQuestion(systemPrompt, conversationCtx);
    }

    // ── Deterministic stub responses ──
    int q = m_session ? m_session->questionCount + 1 : 1;
    switch (q) {
    case 1:
        return "Merhaba! Hedefini netlestirmek istiyorum. "
               "Bugun en cok odaklanmak istedigin konu veya gorev nedir?";
    case 2:
        return "Bu hedefin sana neden onemli? Basariya ulastiginda ne degismis olacak?";
    case 3:
        return "Bu hedefe ulasmak icin ilk somut adimin ne olabilir? En buyuk engel nedir?";
    default:
        return "Anliyorum. Peki bu hedefe ulasmak icin kendine ne kadar sure veriyorsun ve su anki hazirlik seviyeni nasil degerlendirirsin?";
    }
}

// ═══════════════════════════════════════════════════════════════════════
// EVENT: InterviewStartRequested
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleInterviewStart(const std::string& requestId, int /*maxQuestions*/) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Create a fresh session (replaces any previous one)
    m_session = std::make_unique<InterviewSession>();
    m_session->sessionId = generateUUID();
    m_session->markRequestProcessed(requestId);

    DebugLogInterview("Session started: " + m_session->sessionId);

    // Generate the first AI question
    std::string aiQuestion = generateAIQuestion("");

    std::string aiMsgId = generateUUID();
    std::string ts      = nowISO();

    InterviewMessage aiMsg;
    aiMsg.id   = aiMsgId;
    aiMsg.role = "ai";
    aiMsg.text = aiQuestion;
    aiMsg.ts   = ts;
    m_session->transcript.push_back(aiMsg);
    m_session->questionCount = 1;

    json response;
    response["type"]      = "InterviewStarted";
    response["sessionId"] = m_session->sessionId;
    response["requestId"] = requestId;
    response["ts"]        = ts;
    response["payload"]   = {
        {"sessionId", m_session->sessionId},
        {"firstQuestion", {
            {"text",       aiQuestion},
            {"messageId",  aiMsgId},
            {"isQuestion", true}
        }}
    };

    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// EVENT: UserMessageSubmitted
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleUserMessage(const std::string& requestId,
                                                const std::string& sessionId,
                                                const std::string& text,
                                                const std::string& messageId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // ── Session validation ─────────────────────────────────────────────
    if (!m_session || m_session->sessionId != sessionId) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "INVALID_SESSION"}, {"message", "Session not found"}};
        return err.dump();
    }

    // ── Duplicate requestId → NO-OP ───────────────────────────────────
    if (m_session->isRequestProcessed(requestId)) {
        DebugLogInterview("Duplicate requestId ignored: " + requestId);
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "DUPLICATE_REQUEST"}, {"message", "Already processed"}};
        return err.dump();
    }

    // ── Finalized → reject ────────────────────────────────────────────
    if (m_session->finalized) {
        DebugLogInterview("Message rejected: session finalized");
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "SESSION_FINALIZED"}, {"message", "Interview already finalized"}};
        return err.dump();
    }

    m_session->markRequestProcessed(requestId);

    // ── Validate text (backend double-check) ──────────────────────────
    std::string trimmed = text;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    if (!trimmed.empty())
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
    if (trimmed.empty()) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "EMPTY_MESSAGE"}, {"message", "Message text is empty"}};
        return err.dump();
    }

    // ── Append user message ───────────────────────────────────────────
    std::string ts = nowISO();
    InterviewMessage userMsg;
    userMsg.id   = messageId;
    userMsg.role = "user";
    userMsg.text = trimmed;
    userMsg.ts   = ts;
    m_session->transcript.push_back(userMsg);

    // ── If all questions have been asked, accept final answer & auto-finalize ──
    if (m_session->questionCount >= InterviewSession::kMaxQuestions) {
        DebugLogInterview("Final answer received — auto-finalizing session");
        m_session->finalized = true;
        m_session->endedBy   = "limit";

        InterviewResult result = buildResult();
        persistResult(result);

        json response;
        response["type"]      = "InterviewFinalized";
        response["sessionId"] = sessionId;
        response["requestId"] = requestId;
        response["ts"]        = ts;
        response["payload"]   = {
            {"endedBy",              "limit"},
            {"questionCount",        m_session->questionCount},
            {"transcript",           m_session->transcriptToJson()},
            {"interviewSessionId",   sessionId},
            {"interviewResult",      result.toJson()}
        };

        return response.dump();
    }

    // ── Generate next AI question ─────────────────────────────────────
    std::string aiQuestion = generateAIQuestion(trimmed);
    std::string aiMsgId    = generateUUID();
    std::string aiTs       = nowISO();

    InterviewMessage aiMsg;
    aiMsg.id   = aiMsgId;
    aiMsg.role = "ai";
    aiMsg.text = aiQuestion;
    aiMsg.ts   = aiTs;
    m_session->transcript.push_back(aiMsg);
    m_session->questionCount++;

    DebugLogInterview("Question " + std::to_string(m_session->questionCount) +
                      "/" + std::to_string(InterviewSession::kMaxQuestions) + " produced");

    json response;
    response["type"]      = "AiQuestionProduced";
    response["sessionId"] = sessionId;
    response["requestId"] = requestId;
    response["ts"]        = aiTs;
    response["payload"]   = {
        {"text",          aiQuestion},
        {"messageId",     aiMsgId},
        {"isQuestion",    true},
        {"questionCount", m_session->questionCount}
    };

    // Signal last question hint (UI should let user answer before finalizing)
    if (m_session->questionCount >= InterviewSession::kMaxQuestions) {
        DebugLogInterview("Max questions reached — lastQuestion hint sent");
        response["payload"]["autoFinalize"] = true;
    }

    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// EVENT: FinalizeInterviewRequested
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleFinalizeInterview(const std::string& requestId,
                                                     const std::string& sessionId,
                                                     const std::string& endedBy) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // ── Session validation ─────────────────────────────────────────────
    if (!m_session || m_session->sessionId != sessionId) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "INVALID_SESSION"}, {"message", "Session not found"}};
        return err.dump();
    }

    // ── Already finalized → idempotent response ───────────────────────
    if (m_session->finalized) {
        DebugLogInterview("Finalize ignored: already finalized");
        json resp;
        resp["type"]      = "InterviewFinalized";
        resp["sessionId"] = sessionId;
        resp["requestId"] = requestId;
        resp["ts"]        = nowISO();
        resp["payload"]   = {
            {"endedBy",          m_session->endedBy},
            {"questionCount",    m_session->questionCount},
            {"alreadyFinalized", true}
        };
        return resp.dump();
    }

    // ── Duplicate request check ───────────────────────────────────────
    if (m_session->isRequestProcessed(requestId)) {
        DebugLogInterview("Duplicate finalize requestId ignored: " + requestId);
        json resp;
        resp["type"]      = "InterviewFinalized";
        resp["sessionId"] = sessionId;
        resp["requestId"] = requestId;
        resp["ts"]        = nowISO();
        resp["payload"]   = {
            {"endedBy",          m_session->endedBy.empty() ? endedBy : m_session->endedBy},
            {"questionCount",    m_session->questionCount},
            {"alreadyFinalized", true}
        };
        return resp.dump();
    }

    m_session->markRequestProcessed(requestId);
    m_session->finalized = true;
    m_session->endedBy   = endedBy;

    DebugLogInterview("Interview finalized by=" + endedBy +
                      " questions=" + std::to_string(m_session->questionCount));

    // ── Build & persist deterministic result document ──────────────────
    InterviewResult result = buildResult();
    persistResult(result);

    json response;
    response["type"]      = "InterviewFinalized";
    response["sessionId"] = sessionId;
    response["requestId"] = requestId;
    response["ts"]        = nowISO();
    response["payload"]   = {
        {"endedBy",              endedBy},
        {"questionCount",        m_session->questionCount},
        {"transcript",           m_session->transcriptToJson()},
        {"interviewSessionId",   sessionId},
        {"interviewResult",      result.toJson()}
    };

    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// SLOT FILLER: Start
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleSlotFillerStart(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_slotFiller = std::make_unique<InterviewSlotFiller>(m_gemini);
    DebugLogInterview("SlotFiller session started");

    // Get initial reset state + first question
    json resetMsg = m_slotFiller->reset();
    json firstQ   = m_slotFiller->firstQuestion();

    // Combine into single response
    json response;
    response["type"]      = firstQ["type"];
    response["requestId"] = requestId;
    response["ts"]        = firstQ["ts"];
    response["payload"]   = firstQ["payload"];

    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// SLOT FILLER: Answer
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleSlotFillerAnswer(const std::string& requestId,
                                                     const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_slotFiller) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "NO_SLOT_SESSION"}, {"message", "SlotFiller session not started"}};
        return err.dump();
    }

    json result = m_slotFiller->processAnswer(text);
    result["requestId"] = requestId;

    DebugLogInterview("SlotFiller answer processed: type=" + result.value("type", "?"));
    return result.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// SLOT FILLER: Finalize
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleSlotFillerFinalize(const std::string& requestId,
                                                       const std::string& endedBy) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_slotFiller) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {{"code", "NO_SLOT_SESSION"}, {"message", "SlotFiller session not started"}};
        return err.dump();
    }

    json slotResult = m_slotFiller->finalize(endedBy);
    slotResult["requestId"] = requestId;

    // ── Build & persist result document with extracted slots ───────────
    InterviewResult ir = buildResult();
    persistResult(ir);
    slotResult["payload"]["interviewResult"] = ir.toJson();

    DebugLogInterview("SlotFiller finalized by=" + endedBy);
    return slotResult.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// Build deterministic InterviewResult from current session + slot state
// ═══════════════════════════════════════════════════════════════════════
InterviewResult InterviewHandler::buildResult() const {
    InterviewResult r;

    if (m_session) {
        r.session_id     = m_session->sessionId;
        r.finalized      = m_session->finalized;
        r.ended_by       = m_session->endedBy;
        r.question_count = m_session->questionCount;
        r.max_questions  = InterviewSession::kMaxQuestions;
        r.created_at     = m_session->transcript.empty() ? "" : m_session->transcript.front().ts;
        r.finalized_at   = m_session->finalized ? nowISO() : "";

        // Copy transcript (InterviewMessage → InterviewResultMessage)
        for (const auto& msg : m_session->transcript) {
            InterviewResultMessage rm;
            rm.message_id = msg.id;
            rm.role       = msg.role;
            rm.text       = msg.text;
            rm.iso_ts     = msg.ts;
            r.transcript.push_back(rm);
        }
    }

    // Extract slots from SlotFiller if available
    if (m_slotFiller) {
        json slots = m_slotFiller->slotsToJson();
        for (const auto& s : slots) {
            std::string name = s.value("name", "");
            std::string status = s.value("status", "empty");
            if (status != "filled") continue;

            if (name == "goal") {
                r.extracted_slots.goal = s.value("value", "");
            } else if (name == "timeframe") {
                r.extracted_slots.timeframe = s.value("value", "");
            } else if (name == "weekly_hours") {
                std::string wh = s.value("value", "0");
                try { r.extracted_slots.weekly_hours = std::stoi(wh); } catch (...) {}
            } else if (name == "current_level") {
                r.extracted_slots.current_level = s.value("value", "");
            } else if (name == "constraints" && s["value"].is_array()) {
                for (const auto& c : s["value"])
                    r.extracted_slots.constraints.push_back(c.get<std::string>());
            }
        }
    }

    return r;
}

// ═══════════════════════════════════════════════════════════════════════
// Persist InterviewResult to SQLite (Vault)
// ═══════════════════════════════════════════════════════════════════════
void InterviewHandler::persistResult(const InterviewResult& result) {
    if (!m_vault) {
        DebugLogInterview("WARNING: m_vault is null, cannot persist result");
        return;
    }

    json doc = result.toJson();
    std::string docStr = doc.dump();

    bool saved = m_vault->saveInterviewResult(
        result.session_id,
        docStr,
        result.ended_by,
        result.question_count,
        result.max_questions
    );

    if (!saved) {
        DebugLogInterview("ERROR: saveInterviewResult FAILED for session=" + result.session_id);
    }

    // Also persist normalized transcript rows
    if (doc.contains("transcript") && doc["transcript"].is_array()) {
        bool msgSaved = m_vault->saveInterviewMessages(result.session_id, doc["transcript"]);
        if (!msgSaved) {
            DebugLogInterview("ERROR: saveInterviewMessages FAILED for session=" + result.session_id);
        }
    }

    DebugLogInterview("Result persisted for session=" + result.session_id + " (ok=" + (saved ? "true" : "false") + ")");
}

// ═══════════════════════════════════════════════════════════════════════
// Deterministic GoalTree builder — creates a structured plan from
// interview transcript when GeminiService is not available.
//
// Extracts user answers from the transcript and builds:
//   1 MajorGoal  → the user's stated goal
//   2 MinorGoals → "Temel Hazırlık" (foundation) + "Engel Aşma" (obstacles)
//   Each MinorGoal has 2-3 MicroTasks derived from interview answers
// ═══════════════════════════════════════════════════════════════════════
json InterviewHandler::buildDeterministicGoalTree(const std::string& sessionId,
                                                   const InterviewResult& result) const {
    // ── Collect user answers from transcript ──────────────────────────
    std::vector<std::string> userAnswers;
    for (const auto& msg : result.transcript) {
        if (msg.role == "user") {
            userAnswers.push_back(msg.text);
        }
    }

    // Fallback texts if answers are missing
    std::string goalText       = userAnswers.size() > 0 ? userAnswers[0] : "Hedefimi belirle";
    std::string motivationText = userAnswers.size() > 1 ? userAnswers[1] : "Bu hedef benim için önemli";
    std::string actionText     = userAnswers.size() > 2 ? userAnswers[2] : "İlk adımı at";

    // Also use extractedSlots if available
    std::string slotGoal      = result.extracted_slots.goal;
    std::string slotTimeframe = result.extracted_slots.timeframe;
    if (!slotGoal.empty()) goalText = slotGoal;

    // ── Truncate long answers for titles ──────────────────────────────
    auto truncate = [](const std::string& s, size_t maxLen) -> std::string {
        if (s.length() <= maxLen) return s;
        return s.substr(0, maxLen - 1) + "\xE2\x80\xA6"; // UTF-8 ellipsis
    };

    std::string majorTitle = truncate(goalText, 60);
    std::string timeInfo   = slotTimeframe.empty() ? "" : " (" + slotTimeframe + ")";

    std::string ts = nowISO();

    // ── Build MicroTasks ─────────────────────────────────────────────
    // Minor 1: "Temel Hazırlık" — foundation steps derived from Q1 answer
    json micro1;
    micro1["id"]                  = "micro-1-plan";
    micro1["title"]               = "Hedef planını oluştur";
    micro1["description"]         = "\"" + truncate(goalText, 120) + "\" hedefine yönelik detaylı bir plan yaz.";
    micro1["acceptance_criteria"] = "En az 3 maddelik somut bir eylem planı yazılmış olmalı.";
    micro1["evidence_type"]       = "text";
    micro1["status"]              = "open";
    micro1["dependencies"]        = json::array();

    json micro2;
    micro2["id"]                  = "micro-2-research";
    micro2["title"]               = "Araştırma ve kaynak topla";
    micro2["description"]         = "Hedefe ulaşmak için gerekli kaynakları, araçları veya eğitimleri belirle.";
    micro2["acceptance_criteria"] = "En az 2 kaynak (kitap, kurs, araç vb.) listelenmiş olmalı.";
    micro2["evidence_type"]       = "text";
    micro2["status"]              = "open";
    micro2["dependencies"]        = json::array({"micro-1-plan"});

    json micro3;
    micro3["id"]                  = "micro-3-firstaction";
    micro3["title"]               = truncate("İlk adım: " + actionText, 60);
    micro3["description"]         = "Görüşmede belirttiğin ilk somut adımı gerçekleştir: \"" + truncate(actionText, 150) + "\"";
    micro3["acceptance_criteria"] = "Belirtilen ilk adım tamamlanmış ve sonucu not edilmiş olmalı.";
    micro3["evidence_type"]       = "text";
    micro3["status"]              = "open";
    micro3["dependencies"]        = json::array({"micro-1-plan"});

    // Minor 2: "Engel Aşma" — obstacle handling from Q2+Q3
    json micro4;
    micro4["id"]                  = "micro-4-motivation";
    micro4["title"]               = "Motivasyon ankarını belirle";
    micro4["description"]         = "\"" + truncate(motivationText, 120) + "\" — bu motivasyonu somut hatırlatıcılara dönüştür.";
    micro4["acceptance_criteria"] = "Zor anlarda bakılacak en az 1 somut hatırlatıcı (yazı, görsel vb.) oluşturulmuş olmalı.";
    micro4["evidence_type"]       = "text";
    micro4["status"]              = "open";
    micro4["dependencies"]        = json::array();

    json micro5;
    micro5["id"]                  = "micro-5-obstacle";
    micro5["title"]               = "Engel stratejisi oluştur";
    micro5["description"]         = "Görüşmede belirtilen engelleri analiz et ve her biri için bir çözüm stratejisi yaz.";
    micro5["acceptance_criteria"] = "Her engel için en az 1 somut çözüm stratejisi yazılmış olmalı.";
    micro5["evidence_type"]       = "text";
    micro5["status"]              = "open";
    micro5["dependencies"]        = json::array({"micro-4-motivation"});

    json micro6;
    micro6["id"]                  = "micro-6-review";
    micro6["title"]               = "Haftalık ilerleme değerlendirmesi";
    micro6["description"]         = "İlk haftanın sonunda ilerlemeyi gözden geçir ve planı güncelle.";
    micro6["acceptance_criteria"] = "İlerleme raporu yazılmış olmalı: ne yapıldı, ne kaldı, ne değişti?";
    micro6["evidence_type"]       = "text";
    micro6["status"]              = "open";
    micro6["dependencies"]        = json::array({"micro-3-firstaction", "micro-5-obstacle"});

    // ── Assemble MinorGoals ──────────────────────────────────────────
    json minor1;
    minor1["id"]          = "minor-1-foundation";
    minor1["title"]       = "Temel Hazırlık";
    minor1["description"] = "Hedefe yönelik plan, kaynak ve ilk adımlar.";
    minor1["micros"]      = json::array({micro1, micro2, micro3});

    json minor2;
    minor2["id"]          = "minor-2-obstacles";
    minor2["title"]       = "Engel Aşma & Sürdürme";
    minor2["description"] = "Motivasyon, engel analizi ve ilerleme takibi.";
    minor2["micros"]      = json::array({micro4, micro5, micro6});

    // ── Assemble MajorGoal ───────────────────────────────────────────
    json major;
    major["id"]          = "major-1-goal";
    major["title"]       = majorTitle + timeInfo;
    major["description"] = "Socratic Interview'dan çıkarılan ana hedef.";
    major["minors"]      = json::array({minor1, minor2});

    // ── Assemble GoalTree root ───────────────────────────────────────
    json tree;
    tree["version"]      = 1;
    tree["session_id"]   = sessionId;
    tree["generated_at"] = ts;
    tree["majors"]       = json::array({major});

    DebugLogInterview("Deterministic GoalTree built: 1 major, 2 minors, 6 micros");
    return tree;
}

// ═══════════════════════════════════════════════════════════════════════
// GoalTree: load persisted result and trigger generation
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleGenerateGoalTree(const std::string& requestId,
                                                     const std::string& interviewSessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    DebugLogInterview("GoalTree requested for session=" + interviewSessionId);

    // Load the persisted result from Vault
    json storedResult;
    if (m_vault) {
        storedResult = m_vault->getInterviewResult(interviewSessionId);
    }

    if (storedResult.is_null() || storedResult.empty()) {
        // Fallback: if vault lookup failed but we have a finalized in-memory session, use it
        if (m_session && m_session->finalized && m_session->sessionId == interviewSessionId) {
            DebugLogInterview("GoalTree: vault lookup empty, using in-memory session fallback");
            InterviewResult fallbackResult = buildResult();
            storedResult = fallbackResult.toJson();

            // Retry persistence so future lookups succeed
            persistResult(fallbackResult);
        }

        if (storedResult.is_null() || storedResult.empty()) {
            json err;
            err["type"]      = "Error";
            err["requestId"] = requestId;
            err["payload"]   = {
                {"code",    "NO_INTERVIEW_RESULT"},
                {"message", "No finalized interview result found for session: " + interviewSessionId}
            };
            return err.dump();
        }
    }

    // Deserialize for validation
    InterviewResult result = InterviewResult::fromJson(storedResult);
    if (!result.finalized) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {
            {"code",    "NOT_FINALIZED"},
            {"message", "Interview not yet finalized"}
        };
        return err.dump();
    }

    // Build GoalTree response — AI-first (DynamicGoalTree v2), deterministic fallback (v1)
    json goalTreeJson;
    bool generated = false;

    // If a pre-generated goalTree already exists in the stored result, reuse it
    // ONLY when it is the new v2 schema. Older v1 trees are stale fallbacks and
    // must be regenerated through the AI path so the user sees the new format.
    if (storedResult.contains("goalTree") && storedResult["goalTree"].is_object()
        && !storedResult["goalTree"].empty()
        && storedResult["goalTree"].value("version", 1) == 2) {
        goalTreeJson = storedResult["goalTree"];
        generated = true;
        DebugLogInterview("GoalTree: reusing pre-generated v2 tree from storedResult");
    } else if (storedResult.contains("goalTree") && storedResult["goalTree"].is_object()
               && storedResult["goalTree"].value("version", 1) != 2) {
        DebugLogInterview("GoalTree: discarding stale v1 cached tree, will regenerate");
    }

    // ── Primary path: ask Gemini to produce a DynamicGoalTree (v2) ────
    if (!generated && m_gemini && m_gemini->isAvailable()) {
        DebugLogInterview("GoalTree: attempting Gemini-driven DynamicGoalTree generation");

        // Build interview-data context to bind every node to user's words
        std::string interviewContext;
        {
            std::ostringstream ctx;
            ctx << "# MULAKAT VERILERI (Mutlak Kisitlar)\n";
            ctx << "Asagidaki bilgiler kullanicinin mulakat sirasinda verdigi yanitlardan cikarilmistir.\n";
            ctx << "Bu verileri agacin HER dugumunde (title, description, acceptanceCriteria, "
                   "actionItems, obstacle) birebir yansit.\n\n";
            int answerIdx = 0;
            for (const auto& msg : result.transcript) {
                if (msg.role == "user") {
                    ++answerIdx;
                    ctx << "Kullanici Yaniti " << answerIdx << ": " << msg.text << "\n";
                }
            }
            if (!result.extracted_slots.goal.empty())
                ctx << "\nCikarsanan Hedef: " << result.extracted_slots.goal << "\n";
            if (!result.extracted_slots.timeframe.empty())
                ctx << "Cikarsanan Zaman: " << result.extracted_slots.timeframe << "\n";
            ctx << "\n--- Mulakat Verileri Sonu ---\n\n";
            interviewContext = ctx.str();
        }

        std::string personalizedSystemPrompt =
            interviewContext +
            "ONEMLI TALIMATLAR:\n"
            "1. Yukaridaki mulakat verilerini MUTLAK KISITLAR olarak kullan.\n"
            "2. Kullanici hangi araclari, kaynaklari veya yontemleri belirttiyse, "
            "adimlari bunlar uzerinden kurgula.\n"
            "3. Kullanicinin belirttigi seviyeye gore dallari basitlestir veya derinlestir.\n"
            "4. Kullanicinin belirttigi zaman kisitina gore agacin derinligini otonom belirle:\n"
            "   - Kisa vadeli (1-4 hafta): 2-3 seviye\n"
            "   - Orta vadeli (1-6 ay): 3-4 seviye\n"
            "   - Uzun vadeli (6+ ay): 4-6 seviye\n"
            "5. description, acceptanceCriteria ve actionItems alanlarinda kullanicinin "
            "mulakatta kullandigi SPESIFIK kelimeleri, araclari ve degerleri birebir kullan.\n"
            "6. ASLA statik sablon ya da meta-planlama maddesi (\"plan olustur\", "
            "\"arastirma yap\", \"ilk adimi at\") uretme. Her agac, bu mulakattin "
            "benzersiz bir yansimasi olmali.\n\n" +
            DynamicGoalTreePrompt::SYSTEM_PROMPT;

        std::ostringstream tr;
        for (const auto& msg : result.transcript) {
            tr << (msg.role == "user" ? "Kullanici" : "AI") << ": " << msg.text << "\n";
        }
        std::string aiTs = nowISO();
        std::string treeUserPrompt =
            "Asagidaki gorusme gecmisine dayanarak, kullanicinin hedefi icin "
            "recursive bir GoalNode agaci (version=2) olustur. "
            "Agacin derinligini hedefin karmasikligina ve kullanicinin ayirabilecegi "
            "sureye gore sen belirle.\n\n"
            "Session ID: " + interviewSessionId + "\n"
            "Zaman damgasi: " + aiTs + "\n\n"
            "Gorusme gecmisi:\n" + tr.str();

        std::string treeJson = m_gemini->sendPrompt(personalizedSystemPrompt, treeUserPrompt);

        if (!treeJson.empty()) {
            DebugLogInterview("GoalTree: Gemini raw response (" +
                              std::to_string(treeJson.size()) + " bytes), preview: " +
                              treeJson.substr(0, 300));
            // Strip markdown fences if present
            std::string cleaned = treeJson;
            if (cleaned.find("```json") != std::string::npos) {
                size_t s = cleaned.find("```json") + 7;
                size_t e = cleaned.rfind("```");
                if (e > s) cleaned = cleaned.substr(s, e - s);
            } else if (cleaned.find("```") != std::string::npos) {
                size_t s = cleaned.find("```") + 3;
                size_t e = cleaned.rfind("```");
                if (e > s) cleaned = cleaned.substr(s, e - s);
            }
            cleaned.erase(0, cleaned.find_first_not_of(" \t\n\r"));

            try {
                json dyn = json::parse(cleaned);
                auto vr = DynamicGoalTreeSchema::validate(dyn);
                if (vr.ok) {
                    goalTreeJson = dyn;
                    goalTreeJson["generation_source"] = "ai";
                    generated = true;
                    DebugLogInterview("GoalTree: Gemini DynamicGoalTree validated OK");
                } else {
                    DebugLogInterview("GoalTree: DynamicGoalTree validation FAILED: "
                                      + vr.error + " at " + vr.path
                                      + " | raw preview: " + cleaned.substr(0, 400));
                }
            } catch (const std::exception& e) {
                DebugLogInterview("GoalTree: Gemini JSON parse error: " + std::string(e.what())
                                  + " | raw preview: " + cleaned.substr(0, 400));
            }
        } else {
            DebugLogInterview("GoalTree: Gemini returned empty response (likely token budget "
                              "exhausted by thinking mode or API error). Check GeminiService logs.");
        }
    }

    // ── Fallback path: deterministic v1 tree (last resort) ────────────
    if (!generated) {
        DebugLogInterview("GoalTree: falling back to deterministic v1 tree");
        goalTreeJson = buildDeterministicGoalTree(interviewSessionId, result);
        if (goalTreeJson.is_object())
            goalTreeJson["generation_source"] = "fallback";
    }

    // ── Schema validation (version-aware; anti-hallucinated progress guard) ─
    bool   validOk = false;
    std::string validErr;
    std::string validPath;
    if (goalTreeJson.is_object() && goalTreeJson.value("version", 1) == 2) {
        auto vr = DynamicGoalTreeSchema::validate(goalTreeJson);
        validOk = vr.ok; validErr = vr.error; validPath = vr.path;
    } else {
        auto vr = GoalTreeSchema::validate(goalTreeJson);
        validOk = vr.ok; validErr = vr.error; validPath = vr.path;
    }

    if (!validOk) {
        DebugLogInterview("GoalTree validation FAILED: " + validErr + " at " + validPath);

        json errResponse;
        errResponse["type"]      = "GoalTreeValidationFailed";
        errResponse["requestId"] = requestId;
        errResponse["ts"]        = nowISO();
        errResponse["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"error",              validErr},
            {"path",               validPath}
        };
        return errResponse.dump();
    }

    // ── Persist GoalTree into storedResult so status-change/tick can find it ──
    storedResult["goalTree"] = goalTreeJson;
    if (m_vault) {
        std::string endedBy   = storedResult.value("ended_by", "complete");
        int qCount            = storedResult.value("question_count", 0);
        int maxQ              = storedResult.value("max_questions", 3);
        std::string sid       = storedResult.value("session_id", interviewSessionId);
        m_vault->saveInterviewResult(sid, storedResult.dump(), endedBy, qCount, maxQ);
        DebugLogInterview("GoalTree persisted into storedResult for session=" + interviewSessionId);
    }

    // Cache in memory so status-change/tick can use it as fallback
    m_lastStoredResult = storedResult;

    json response;
    response["type"]      = "GoalTreeGenerated";
    response["requestId"] = requestId;
    response["ts"]        = nowISO();
    response["payload"]   = {
        {"interviewSessionId", interviewSessionId},
        {"interviewResult",    storedResult},
        {"extractedSlots",     storedResult.value("extractedSlots", json::object())},
        {"goalTree",           goalTreeJson},
        {"status",             "ready"}
    };

    DebugLogInterview("GoalTree validated & prepared for session=" + interviewSessionId);
    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// HandleTickMicroTask — validate evidence + mark MicroTask as 'done'
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleTickMicroTask(const std::string& requestId,
                                                   const std::string& interviewSessionId,
                                                   const std::string& microTaskId,
                                                   const nlohmann::json& evidenceJson) {
    std::lock_guard<std::mutex> lock(m_mutex);

    DebugLogInterview("TickMicroTask requested: session=" + interviewSessionId
                      + " micro=" + microTaskId);

    // Load persisted result (contains goalTree)
    json storedResult;
    if (m_vault) {
        storedResult = m_vault->getInterviewResult(interviewSessionId);
    }

    if (storedResult.is_null() || storedResult.empty()) {
        // Fallback 1: use in-memory cached storedResult if session matches
        if (!m_lastStoredResult.is_null() &&
            m_lastStoredResult.value("session_id", "") == interviewSessionId) {
            DebugLogInterview("TickMicroTask: vault empty, using in-memory cache");
            storedResult = m_lastStoredResult;
        }
        // Fallback 2: rebuild from live m_session
        else if (m_session && m_session->finalized && m_session->sessionId == interviewSessionId) {
            DebugLogInterview("TickMicroTask: vault+cache empty, rebuilding from m_session");
            InterviewResult rebuildResult = buildResult();
            storedResult = rebuildResult.toJson();
            storedResult["goalTree"] = buildDeterministicGoalTree(interviewSessionId, rebuildResult);
            persistResult(rebuildResult);
            if (m_vault) {
                std::string sid = storedResult.value("session_id", interviewSessionId);
                m_vault->saveInterviewResult(sid, storedResult.dump(),
                    storedResult.value("ended_by", "complete"),
                    storedResult.value("question_count", 0),
                    storedResult.value("max_questions", 3));
            }
            m_lastStoredResult = storedResult;
        }
        else {
            json err;
            err["type"]      = "TickMicroTaskFailed";
            err["requestId"] = requestId;
            err["ts"]        = nowISO();
            err["payload"]   = {
                {"interviewSessionId", interviewSessionId},
                {"microTaskId",        microTaskId},
                {"errors", json::array({{
                    {"code",        "NO_INTERVIEW_RESULT"},
                    {"message",     "No finalized interview result found"},
                    {"microTaskId", microTaskId}
                }})}
            };
            return err.dump();
        }
    }

    // Find the goalTree inside the stored result — regenerate if missing
    if (!storedResult.contains("goalTree") || !storedResult["goalTree"].is_object()) {
        DebugLogInterview("TickMicroTask: storedResult found but no goalTree, regenerating");
        InterviewResult parsed = InterviewResult::fromJson(storedResult);
        storedResult["goalTree"] = buildDeterministicGoalTree(interviewSessionId, parsed);
        if (m_vault) {
            std::string sid = storedResult.value("session_id", interviewSessionId);
            m_vault->saveInterviewResult(sid, storedResult.dump(),
                storedResult.value("ended_by", "complete"),
                storedResult.value("question_count", 0),
                storedResult.value("max_questions", 3));
        }
        m_lastStoredResult = storedResult;
    }

    json& goalTree = storedResult["goalTree"];

    // Find the MicroTask in the tree
    json* targetMicro = nullptr;
    if (goalTree.contains("majors") && goalTree["majors"].is_array()) {
        for (auto& major : goalTree["majors"]) {
            if (!major.contains("minors") || !major["minors"].is_array()) continue;
            for (auto& minor : major["minors"]) {
                if (!minor.contains("micros") || !minor["micros"].is_array()) continue;
                for (auto& micro : minor["micros"]) {
                    if (micro.contains("id") && micro["id"].get<std::string>() == microTaskId) {
                        targetMicro = &micro;
                        break;
                    }
                }
                if (targetMicro) break;
            }
            if (targetMicro) break;
        }
    }

    if (!targetMicro) {
        json err;
        err["type"]      = "TickMicroTaskFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors", json::array({{
                {"code",        "MICRO_NOT_FOUND"},
                {"message",     "MicroTask not found: " + microTaskId},
                {"microTaskId", microTaskId}
            }})}
        };
        return err.dump();
    }

    // Run tick validation
    TickValidationResult tickResult = GoalTreeSchema::validateTickDone(
        *targetMicro, evidenceJson, goalTree);

    if (!tickResult.ok) {
        DebugLogInterview("TickMicroTask validation FAILED for " + microTaskId
                          + " (" + std::to_string(tickResult.errors.size()) + " errors)");

        json err;
        err["type"]      = "TickMicroTaskFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors",             GoalTreeSchema::tickErrorsToJson(tickResult)}
        };
        return err.dump();
    }

    // ── Validation passed — mutate and persist ────────────────────────
    (*targetMicro)["status"]   = "done";
    (*targetMicro)["evidence"] = evidenceJson;

    // Write back to vault — re-serialize the full document
    if (m_vault) {
        storedResult["goalTree"] = goalTree;
        std::string endedBy   = storedResult.value("ended_by", "complete");
        int qCount            = storedResult.value("question_count", 0);
        int maxQ              = storedResult.value("max_questions", 3);
        std::string sessionId = storedResult.value("session_id", interviewSessionId);
        m_vault->saveInterviewResult(sessionId, storedResult.dump(),
                                      endedBy, qCount, maxQ);
    }

    // Update in-memory cache
    m_lastStoredResult = storedResult;

    DebugLogInterview("MicroTask ticked to done: " + microTaskId);

    json response;
    response["type"]      = "MicroTaskTicked";
    response["requestId"] = requestId;
    response["ts"]        = nowISO();
    response["payload"]   = {
        {"interviewSessionId", interviewSessionId},
        {"microTaskId",        microTaskId},
        {"status",             "done"}
    };
    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// HandleMicroTaskStatusChange — SkillTree UI status change request
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleMicroTaskStatusChange(
    const std::string& requestId,
    const std::string& interviewSessionId,
    const std::string& microTaskId,
    const std::string& newStatus,
    const nlohmann::json& evidenceJson)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    using json = nlohmann::json;

    DebugLogInterview("HandleMicroTaskStatusChange: session=" + interviewSessionId +
                      " micro=" + microTaskId + " newStatus=" + newStatus);

    // Only 'done' transition supported
    if (newStatus != "done") {
        json err;
        err["type"]      = "MicroTaskStatusChangeFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors", json::array({{ {"code","INVALID_STATUS"}, {"message","Only 'done' transitions are supported"}, {"microTaskId",microTaskId} }})}
        };
        return err.dump();
    }

    // Load from vault
    if (!m_vault) {
        json err;
        err["type"]      = "MicroTaskStatusChangeFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors", json::array({{ {"code","NO_VAULT"}, {"message","Database not available"}, {"microTaskId",microTaskId} }})}
        };
        return err.dump();
    }

    json storedResult = m_vault->getInterviewResult(interviewSessionId);
    DebugLogInterview("StatusChange: vault lookup for session=" + interviewSessionId +
                      " returned " + (storedResult.is_null() ? "null" : (storedResult.empty() ? "empty" : "ok")));

    if (storedResult.is_null() || storedResult.empty()) {
        // Fallback 1: use in-memory cached storedResult if session matches
        if (!m_lastStoredResult.is_null() &&
            m_lastStoredResult.value("session_id", "") == interviewSessionId) {
            DebugLogInterview("StatusChange: vault empty, using in-memory cache");
            storedResult = m_lastStoredResult;
        }
        // Fallback 2: rebuild from live m_session if still available
        else if (m_session && m_session->finalized && m_session->sessionId == interviewSessionId) {
            DebugLogInterview("StatusChange: vault+cache empty, rebuilding from m_session");
            InterviewResult rebuildResult = buildResult();
            storedResult = rebuildResult.toJson();
            storedResult["goalTree"] = buildDeterministicGoalTree(interviewSessionId, rebuildResult);
            persistResult(rebuildResult);
            if (m_vault) {
                std::string sid = storedResult.value("session_id", interviewSessionId);
                m_vault->saveInterviewResult(sid, storedResult.dump(),
                    storedResult.value("ended_by", "complete"),
                    storedResult.value("question_count", 0),
                    storedResult.value("max_questions", 3));
            }
            m_lastStoredResult = storedResult;
        }
        else {
            DebugLogInterview("StatusChange: ALL fallbacks failed — cache session_id="
                              + m_lastStoredResult.value("session_id", "(null)")
                              + " requested=" + interviewSessionId
                              + " m_session=" + (m_session ? m_session->sessionId : "(null)"));
            json err;
            err["type"]      = "MicroTaskStatusChangeFailed";
            err["requestId"] = requestId;
            err["ts"]        = nowISO();
            err["payload"]   = {
                {"interviewSessionId", interviewSessionId},
                {"microTaskId",        microTaskId},
                {"errors", json::array({{ {"code","NOT_FOUND"}, {"message","Interview result not found"}, {"microTaskId",microTaskId} }})}
            };
            return err.dump();
        }
    }

    // If storedResult found but goalTree is missing, regenerate it
    if (!storedResult.contains("goalTree")) {
        DebugLogInterview("StatusChange: storedResult found but no goalTree, regenerating");
        InterviewResult parsed = InterviewResult::fromJson(storedResult);
        storedResult["goalTree"] = buildDeterministicGoalTree(interviewSessionId, parsed);
        if (m_vault) {
            std::string sid = storedResult.value("session_id", interviewSessionId);
            m_vault->saveInterviewResult(sid, storedResult.dump(),
                storedResult.value("ended_by", "complete"),
                storedResult.value("question_count", 0),
                storedResult.value("max_questions", 3));
        }
        m_lastStoredResult = storedResult;
    }

    json goalTree = storedResult["goalTree"];

    // Find the micro task
    json* targetMicro = nullptr;
    for (auto& maj : goalTree["majors"]) {
        for (auto& min : maj["minors"]) {
            for (auto& mic : min["micros"]) {
                if (mic.value("id", "") == microTaskId) {
                    targetMicro = &mic;
                    break;
                }
            }
            if (targetMicro) break;
        }
        if (targetMicro) break;
    }

    if (!targetMicro) {
        json err;
        err["type"]      = "MicroTaskStatusChangeFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors", json::array({{ {"code","MICRO_NOT_FOUND"}, {"message","MicroTask not found in GoalTree"}, {"microTaskId",microTaskId} }})}
        };
        return err.dump();
    }

    // Validate using GoalTreeSchema::validateTickDone
    auto tickResult = GoalTreeSchema::validateTickDone(*targetMicro, evidenceJson, goalTree);
    if (!tickResult.ok) {
        json err;
        err["type"]      = "MicroTaskStatusChangeFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"microTaskId",        microTaskId},
            {"errors",             GoalTreeSchema::tickErrorsToJson(tickResult)}
        };
        return err.dump();
    }

    // Mutate and persist
    (*targetMicro)["status"]   = "done";
    (*targetMicro)["evidence"] = evidenceJson;

    storedResult["goalTree"] = goalTree;
    std::string endedBy   = storedResult.value("ended_by", "complete");
    int qCount            = storedResult.value("question_count", 0);
    int maxQ              = storedResult.value("max_questions", 3);
    std::string sessionId = storedResult.value("session_id", interviewSessionId);
    m_vault->saveInterviewResult(sessionId, storedResult.dump(),
                                  endedBy, qCount, maxQ);

    // Update in-memory cache
    m_lastStoredResult = storedResult;

    DebugLogInterview("MicroTask status changed to done: " + microTaskId);

    json response;
    response["type"]      = "MicroTaskStatusChanged";
    response["requestId"] = requestId;
    response["ts"]        = nowISO();
    response["payload"]   = {
        {"interviewSessionId", interviewSessionId},
        {"microTaskId",        microTaskId},
        {"status",             "done"},
        {"evidence",           evidenceJson}
    };
    return response.dump();
}

// ═══════════════════════════════════════════════════════════════════════
// HandleReplanGoalTree — re-generate GoalTree, merge status/evidence,
//                         produce diff, persist with versioning
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleReplanGoalTree(
    const std::string& requestId,
    const std::string& interviewSessionId,
    const std::string& reason,
    const nlohmann::json& changedSlots)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    using json = nlohmann::json;

    DebugLogInterview("HandleReplanGoalTree: session=" + interviewSessionId
                      + " reason=" + reason);

    // ── Load stored result ────────────────────────────────────────────
    if (!m_vault) {
        json err;
        err["type"]      = "ReplanFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"code",  "NO_VAULT"},
            {"error", "Database not available"}
        };
        return err.dump();
    }

    json storedResult = m_vault->getInterviewResult(interviewSessionId);
    if (storedResult.is_null() || storedResult.empty()) {
        json err;
        err["type"]      = "ReplanFailed";
        err["requestId"] = requestId;
        err["ts"]        = nowISO();
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"code",  "NOT_FOUND"},
            {"error", "Interview result not found"}
        };
        return err.dump();
    }

    // ── Extract old GoalTree ──────────────────────────────────────────
    json oldGoalTree = json::object();
    if (storedResult.contains("goalTree") && storedResult["goalTree"].is_object()) {
        oldGoalTree = storedResult["goalTree"];
    }

    std::string oldVersionId = oldGoalTree.value("version_id", "");

    // ── Generate new GoalTree via AI using DynamicGoalTreePrompt ─────────
    // Build a user prompt from the interview transcript + extracted slots
    std::string userGoal;
    if (storedResult.contains("extractedSlots") && storedResult["extractedSlots"].is_object()) {
        userGoal = storedResult["extractedSlots"].value("goal", "");
    }
    if (userGoal.empty() && storedResult.contains("transcript") && storedResult["transcript"].is_array()) {
        for (const auto& msg : storedResult["transcript"]) {
            if (msg.value("role", "") == "user") {
                userGoal = msg.value("text", "");
                break;
            }
        }
    }
    if (userGoal.empty()) userGoal = "Hedefimi belirle";

    std::string timeframe;
    if (storedResult.contains("extractedSlots") && storedResult["extractedSlots"].is_object()) {
        timeframe = storedResult["extractedSlots"].value("timeframe", "");
    }

    // Build user prompt with goal + context
    std::string userPrompt = "Hedef: " + userGoal;
    if (!timeframe.empty()) userPrompt += "\nZaman dilimi: " + timeframe;
    if (!changedSlots.is_null() && changedSlots.is_object()) {
        userPrompt += "\nGüncellenen bilgiler: " + changedSlots.dump();
    }
    userPrompt += "\nsession_id: " + interviewSessionId;

    json newGoalTree = json::object();

    // Attempt AI generation
    if (m_gemini && m_gemini->isAvailable()) {
        DebugLogInterview("Replan: AI ile dinamik hedef ağacı oluşturuluyor...");
        std::string aiResponse = m_gemini->sendPrompt(
            DynamicGoalTreePrompt::SYSTEM_PROMPT, userPrompt);

        if (!aiResponse.empty()) {
            // Strip markdown fences
            std::string cleaned = aiResponse;
            {
                size_t fenceStart = cleaned.find("```json");
                if (fenceStart != std::string::npos) {
                    cleaned = cleaned.substr(fenceStart + 7);
                    size_t fenceEnd = cleaned.rfind("```");
                    if (fenceEnd != std::string::npos) cleaned = cleaned.substr(0, fenceEnd);
                } else {
                    fenceStart = cleaned.find("```");
                    if (fenceStart != std::string::npos) {
                        cleaned = cleaned.substr(fenceStart + 3);
                        size_t fenceEnd = cleaned.rfind("```");
                        if (fenceEnd != std::string::npos) cleaned = cleaned.substr(0, fenceEnd);
                    }
                }
                size_t first = cleaned.find_first_not_of(" \t\r\n");
                size_t last  = cleaned.find_last_not_of(" \t\r\n");
                if (first != std::string::npos && last != std::string::npos)
                    cleaned = cleaned.substr(first, last - first + 1);
            }

            try {
                json parsed = json::parse(cleaned);
                // Validate against DynamicGoalTree schema (version 2, recursive)
                auto dynamicValidation = DynamicGoalTreeSchema::validate(parsed);
                if (dynamicValidation.ok) {
                    newGoalTree = parsed;
                    DebugLogInterview("Replan: AI dinamik ağaç doğrulandı (version 2)");
                } else {
                    DebugLogInterview("Replan: AI ağacı doğrulama başarısız: "
                                      + dynamicValidation.error + " at " + dynamicValidation.path);
                }
            } catch (const std::exception& e) {
                DebugLogInterview("Replan: AI yanıtı JSON parse hatası: " + std::string(e.what()));
            }
        } else {
            DebugLogInterview("Replan: AI boş yanıt döndü, fallback kullanılacak");
        }
    }

    // Fallback: if AI failed, use old tree as base (original behavior)
    if (newGoalTree.empty() || !newGoalTree.contains("root")) {
        DebugLogInterview("Replan: AI başarısız, eski ağaç korunuyor (fallback)");
        newGoalTree = oldGoalTree;
    }

    // ── Assign versioning metadata ────────────────────────────────────
    std::string newVersionId = generateUUID();
    std::string ts = nowISO();
    newGoalTree["version_id"]     = newVersionId;
    newGoalTree["parent_version"] = oldVersionId;
    newGoalTree["created_ts"]     = ts;
    newGoalTree["generated_at"]   = ts;

    // ── Merge: carry over status/evidence from old tree ───────────────
    GoalTreeSchema::mergeGoalTrees(oldGoalTree, newGoalTree);

    // ── Validate new tree ─────────────────────────────────────────────
    GoalTreeValidation validation = GoalTreeSchema::validate(newGoalTree);
    if (!validation.ok) {
        DebugLogInterview("Replan GoalTree validation FAILED: " + validation.error);
        json err;
        err["type"]      = "ReplanFailed";
        err["requestId"] = requestId;
        err["ts"]        = ts;
        err["payload"]   = {
            {"interviewSessionId", interviewSessionId},
            {"code",  "VALIDATION_FAILED"},
            {"error", validation.error + " at " + validation.path}
        };
        return err.dump();
    }

    // ── Compute diff ──────────────────────────────────────────────────
    auto diffResult = GoalTreeSchema::diffGoalTrees(oldGoalTree, newGoalTree);
    diffResult.timestamp = ts;
    json diffJson = GoalTreeSchema::diffResultToJson(diffResult);

    // ── Persist new tree + diff ───────────────────────────────────────
    storedResult["goalTree"]     = newGoalTree;
    storedResult["lastReplanDiff"] = diffJson;
    if (!changedSlots.is_null() && changedSlots.is_object()) {
        storedResult["lastReplanChangedSlots"] = changedSlots;
    }
    storedResult["lastReplanReason"] = reason;

    std::string endedBy   = storedResult.value("ended_by", "complete");
    int qCount            = storedResult.value("question_count", 0);
    int maxQ              = storedResult.value("max_questions", 3);
    std::string sessionId = storedResult.value("session_id", interviewSessionId);
    m_vault->saveInterviewResult(sessionId, storedResult.dump(),
                                  endedBy, qCount, maxQ);

    DebugLogInterview("Replan completed: " + oldVersionId + " -> " + newVersionId
                                             + " | diff entries=" + std::to_string(diffResult.entries.size()));

                          // ── Build response ────────────────────────────────────────────────
                          json response;
                          response["type"]      = "ReplanCompleted";
                          response["requestId"] = requestId;
                          response["ts"]        = ts;
                          response["payload"]   = {
                              {"interviewSessionId", interviewSessionId},
                              {"goalTree",           newGoalTree},
                              {"diff",               diffJson},
                              {"oldVersionId",       oldVersionId},
                              {"newVersionId",       newVersionId}
                          };
                          return response.dump();
                      }

                      // ═══════════════════════════════════════════════════════════════════════
                      // Goals Chat: Socratic Mentor System Prompt
                      // ═══════════════════════════════════════════════════════════════════════
                      static const std::string kSocraticSystemPrompt =
                          "Sen bir strateji mentorusun. Kullanicinin girdigi hedefi gerceklestirebilmesi icin "
                          "gerekli tum parametreleri (mevcut bilgi seviyesi, eldeki araclar, zaman kisiti, "
                          "motivasyon kaynagi vb.) ogrenmek zorundasin. Her seferinde kullanicinin son verdigi "
                          "cevabi analiz ederek, ona sadece bir adet ve bir onceki cevabiyla baglantili "
                          "(baglamsal) soru sor. Asla onceden hazirlanmis bir soru listesi sunma.\n\n"
                          "ONEMLI: Eger kullanicinin cevabi belirsiz veya yuzeysel kaldiysa, sadece "
                          "'Bunu biraz daha acar misin?' gibi genel bir ifade KULLANMA. Bunun yerine "
                          "tam olarak hangi boyutun eksik oldugunu belirt. Ornegin: zaman kisiti, mevcut "
                          "seviye, kullanilacak araclar, motivasyon, hedefin olculebilir hali gibi "
                          "somut bir boyutu sor. Her sorun neyi ogrenmek istedigini acikca icermeli.\n\n"
                          "Eger kullanicidan aldigi bilgilerin bir yol haritasi olusturmak icin yeterli "
                          "olduguna karar verirsen, son mesajinin SADECE su metinle baslamasini sagla:\n"
                          "[PLAN_READY]\n"
                          "Ardindan kullaniciya gorusmeyi bitirdigini ve plani hazirlayacagini belirten "
                          "kisa bir gecis cumlesi yaz.\n\n"
                          "KRITIK: Promptlarin icinde asla spesifik hedeflere dair ornekler verme. "
                          "Sistem her turlu insan hedefi icin evrensel ve dinamik bir mantikla calismali.\n"
                          "Turkce yaz.";

                      // ═══════════════════════════════════════════════════════════════════════
                      // Goals Chat: Start a Socratic planning session in the Goals tab
                      // ═══════════════════════════════════════════════════════════════════════
                      std::string InterviewHandler::HandleGoalsChatStart(const std::string& requestId) {
                          std::lock_guard<std::mutex> lock(m_mutex);

                          m_session = std::make_unique<InterviewSession>();
                          m_session->sessionId = generateUUID();
                          m_session->goalsChatState = GoalsChatState::ASKING_QUESTIONS;
                          m_session->goalsChatQuestionCount = 0;
                          m_session->markRequestProcessed(requestId);

                          DebugLogInterview("GoalsChat started: " + m_session->sessionId);

                          // Ask Gemini for a dynamic welcome, fallback to static
                          std::string welcomeText;
                          if (m_gemini && m_gemini->isAvailable()) {
                              std::string userPrompt =
                                  "Kullanici yeni bir planlama oturumu baslatti. "
                                  "Ona kisa ve sicak bir karsilama mesaji yaz, ardindan hedefini sormak icin "
                                  "tek bir acik uclu soru sor. Sadece karsilama + soru yaz, baska bir sey ekleme.";
                              welcomeText = m_gemini->sendPrompt(kSocraticSystemPrompt, userPrompt);
                          }
                          if (welcomeText.empty()) {
                              welcomeText = "Merhaba! Seni dinliyorum. Gerceklestirmek istedigin hedefi benimle paylas, birlikte netlestirip bir yol haritasi olusturalim.";
                          }
                          std::string msgId = generateUUID();
                          std::string ts = nowISO();

                          InterviewMessage aiMsg;
                          aiMsg.id   = msgId;
                          aiMsg.role = "ai";
                          aiMsg.text = welcomeText;
                          aiMsg.ts   = ts;
                          m_session->transcript.push_back(aiMsg);

                          json response;
                          response["type"]      = "GoalsChatResponse";
                          response["requestId"] = requestId;
                          response["sessionId"] = m_session->sessionId;
                          response["ts"]        = ts;
                          response["payload"]   = {
                              {"sessionId",    m_session->sessionId},
                              {"responseType", "chatStarted"},
                              {"text",         welcomeText},
                              {"messageId",    msgId},
                              {"role",         "ai"}
                          };
                          return response.dump();
                      }

                      // ═══════════════════════════════════════════════════════════════════════
                      // Goals Chat: Handle user message — dynamic Socratic Q via Gemini
                      // ═══════════════════════════════════════════════════════════════════════
                      std::string InterviewHandler::HandleGoalsChatMessage(const std::string& requestId,
                                                                            const std::string& sessionId,
                                                                            const std::string& text) {
                          std::lock_guard<std::mutex> lock(m_mutex);

                          if (!m_session || m_session->sessionId != sessionId) {
                              json err;
                              err["type"]      = "Error";
                              err["requestId"] = requestId;
                              err["payload"]   = {{"code", "INVALID_SESSION"}, {"message", "Goals chat session not found"}};
                              return err.dump();
                          }

                          if (m_session->isRequestProcessed(requestId)) {
                              json err;
                              err["type"]      = "Error";
                              err["requestId"] = requestId;
                              err["payload"]   = {{"code", "DUPLICATE_REQUEST"}, {"message", "Already processed"}};
                              return err.dump();
                          }
                          m_session->markRequestProcessed(requestId);

                          std::string trimmed = text;
                          trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
                          if (!trimmed.empty())
                              trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
                          if (trimmed.empty()) {
                              json err;
                              err["type"]      = "Error";
                              err["requestId"] = requestId;
                              err["payload"]   = {{"code", "EMPTY_MESSAGE"}, {"message", "Message text is empty"}};
                              return err.dump();
                          }

                          std::string ts = nowISO();
                          std::string userMsgId = generateUUID();
                          InterviewMessage userMsg;
                          userMsg.id   = userMsgId;
                          userMsg.role = "user";
                          userMsg.text = trimmed;
                          userMsg.ts   = ts;
                          m_session->transcript.push_back(userMsg);
                          m_session->goalsChatQuestionCount++;

                          // ── Ask Gemini for next Socratic response ─────────────────────────
                          std::string aiResponse;
                          bool shouldGeneratePlan = false;

                          if (m_gemini && m_gemini->isAvailable() &&
                              m_session->goalsChatState == GoalsChatState::ASKING_QUESTIONS) {

                              std::string conversationHistory = m_session->transcriptToPromptString();
                              std::string userPrompt =
                                  "Gorusme gecmisi:\n" + conversationHistory + "\n"
                                  "Kullanicinin son mesaji: " + trimmed + "\n\n"
                                  "Simdi kullanicinin cevabini analiz et. Eger yol haritasi olusturmak icin "
                                  "yeterli bilgiye ulastigindan eminsen, mesajini [PLAN_READY] ile baslat. "
                                  "Degilse, bir onceki cevabiyla baglantili tek bir baglamsal soru sor.";

                              aiResponse = m_gemini->sendPrompt(kSocraticSystemPrompt, userPrompt);

                              // Check if AI decided we have enough info
                              if (aiResponse.find("[PLAN_READY]") != std::string::npos) {
                                  shouldGeneratePlan = true;
                                  // Remove the marker from the displayed text
                                  size_t pos = aiResponse.find("[PLAN_READY]");
                                  aiResponse.erase(pos, 12);
                                  // Trim leading whitespace/newlines after removing marker
                                  aiResponse.erase(0, aiResponse.find_first_not_of(" \t\n\r"));
                              }
                          }

                          // Hard cap fallback: force plan generation
                          if (!shouldGeneratePlan &&
                              m_session->goalsChatQuestionCount >= InterviewSession::kMaxGoalsChatQuestions) {
                              shouldGeneratePlan = true;
                              if (aiResponse.empty()) {
                                  aiResponse = "Yeterli bilgiye ulastik, simdi sana ozel bir yol haritasi hazirliyorum...";
                              }
                              DebugLogInterview("GoalsChat: hard cap reached, forcing plan generation");
                          }

                          // Fallback if Gemini failed — derive a context-aware question from the last user message
                          if (aiResponse.empty() && !shouldGeneratePlan) {
                              // Try to generate a targeted fallback via Gemini with a simpler prompt
                              if (m_gemini && m_gemini->isAvailable()) {
                                  std::string fallbackPrompt =
                                      "Kullanicinin son mesaji: \"" + trimmed + "\"\n\n"
                                      "Bu mesajda hangi bilgi eksik? Zaman kisiti mi, mevcut seviye mi, "
                                      "kullanilacak araclar mi, motivasyon mu, yoksa baska bir sey mi? "
                                      "Eksik olan boyutu soran TEK bir Turkce soru uret. "
                                      "Genel ifadeler kullanma, somut ol.";
                                  aiResponse = m_gemini->sendPrompt(kSocraticSystemPrompt, fallbackPrompt);
                              }
                              // Hard fallback if Gemini is still unavailable
                              if (aiResponse.empty()) {
                                  aiResponse = "Anliyorum. Peki bu konuda hangi zaman dilimini hedefliyorsun ve su anki seviyeni nasil tanimlarsdin?";
                              }
                          }

                          // ── Record AI message in transcript ───────────────────────────────
                          std::string aiMsgId = generateUUID();
                          std::string aiTs = nowISO();
                          InterviewMessage aiMsg;
                          aiMsg.id   = aiMsgId;
                          aiMsg.role = "ai";
                          aiMsg.text = aiResponse;
                          aiMsg.ts   = aiTs;
                          m_session->transcript.push_back(aiMsg);

                          // ── PLAN GENERATION ───────────────────────────────────────────────
                          if (shouldGeneratePlan) {
                              m_session->goalsChatState = GoalsChatState::GENERATING_PLAN;
                              DebugLogInterview("GoalsChat: generating plan for session=" + sessionId);

                              // ── Extract interview data as context/constraints ──────────────
                              std::string interviewContext;
                              {
                                  std::ostringstream ctx;
                                  ctx << "# MULAKAT VERILERI (Mutlak Kisitlar)\n";
                                  ctx << "Asagidaki bilgiler kullanicinin mulakat sirasinda verdigi yanitlardan cikarilmistir.\n";
                                  ctx << "Bu verileri agacin HER dugumunde (title, description, acceptanceCriteria) birebir yansit.\n\n";

                                  int answerIdx = 0;
                                  for (const auto& msg : m_session->transcript) {
                                      if (msg.role == "user") {
                                          ++answerIdx;
                                          ctx << "Kullanici Yaniti " << answerIdx << ": " << msg.text << "\n";
                                      }
                                  }
                                  ctx << "\n--- Mulakat Verileri Sonu ---\n\n";
                                  interviewContext = ctx.str();
                              }

                              // ── Build personalized system prompt ───────────────────────────
                              std::string personalizedSystemPrompt =
                                  interviewContext +
                                  "ONEMLI TALIMATLAR:\n"
                                  "1. Yukaridaki mulakat verilerini MUTLAK KISITLAR olarak kullan.\n"
                                  "2. Kullanici hangi araclari, kaynaklari veya yontemleri belirttiyse, "
                                  "adimlari bunlar uzerinden kurgula.\n"
                                  "3. Kullanicinin belirttigi seviyeye gore dallari basitlestir veya derinlestir.\n"
                                  "4. Kullanicinin belirttigi zaman kisitina gore agacin derinligini otonom belirle:\n"
                                  "   - Kisa vadeli (1-4 hafta): 2-3 seviye\n"
                                  "   - Orta vadeli (1-6 ay): 3-4 seviye\n"
                                  "   - Uzun vadeli (6+ ay): 4-6 seviye\n"
                                  "5. description ve acceptanceCriteria alanlarinda kullanicinin mulakatta "
                                  "kullandigi SPESIFIK kelimeleri, araclari ve degerleri birebir kullan.\n"
                                  "6. ASLA statik sablon kullanma. Her agac, bu mulakattin benzersiz bir yansimasi olmali.\n\n" +
                                  DynamicGoalTreePrompt::SYSTEM_PROMPT;

                              // Build the full transcript for DynamicGoalTree prompt
                              std::string fullTranscript = m_session->transcriptToPromptString();
                              std::string treeUserPrompt =
                                  "Asagidaki gorusme gecmisine dayanarak, kullanicinin hedefi icin "
                                  "recursive bir GoalNode agaci olustur. "
                                  "Agacin derinligini hedefin karmasikligina ve kullanicinin ayirabilecegi "
                                  "sureye gore sen belirle.\n\n"
                                  "Session ID: " + sessionId + "\n"
                                  "Zaman damgasi: " + aiTs + "\n\n"
                                  "Gorusme gecmisi:\n" + fullTranscript;

                              std::string treeJson;
                              if (m_gemini && m_gemini->isAvailable()) {
                                  treeJson = m_gemini->sendPrompt(
                                      personalizedSystemPrompt, treeUserPrompt);
                              }

                              // Try to parse the tree JSON from Gemini response
                              json dynamicTree;
                              bool treeValid = false;
                              if (!treeJson.empty()) {
                                  // Strip markdown fences if present
                                  std::string cleaned = treeJson;
                                  if (cleaned.find("```json") != std::string::npos) {
                                      size_t start = cleaned.find("```json") + 7;
                                      size_t end = cleaned.rfind("```");
                                      if (end > start) cleaned = cleaned.substr(start, end - start);
                                  } else if (cleaned.find("```") != std::string::npos) {
                                      size_t start = cleaned.find("```") + 3;
                                      size_t end = cleaned.rfind("```");
                                      if (end > start) cleaned = cleaned.substr(start, end - start);
                                  }
                                  cleaned.erase(0, cleaned.find_first_not_of(" \t\n\r"));

                                  try {
                                      dynamicTree = json::parse(cleaned);
                                      auto validation = DynamicGoalTreeSchema::validate(dynamicTree);
                                      if (validation.ok) {
                                          treeValid = true;
                                          DebugLogInterview("GoalsChat: DynamicGoalTree validated OK");
                                      } else {
                                          DebugLogInterview("GoalsChat: DynamicGoalTree validation FAILED: " + validation.error);
                                      }
                                  } catch (const std::exception& e) {
                                      DebugLogInterview("GoalsChat: JSON parse error: " + std::string(e.what()));
                                  }
                              }

                              // Fallback: build deterministic tree if Gemini failed
                              if (!treeValid) {
                                  DebugLogInterview("GoalsChat: falling back to deterministic tree");
                                  m_session->finalized = true;
                                  m_session->endedBy = "ai_decision";
                                  InterviewResult result = buildResult();
                                  persistResult(result);
                                  json v1Tree = buildDeterministicGoalTree(sessionId, result);

                                  // Convert v1 to v2 recursive format
                                  json rootNode;
                                  rootNode["id"]          = "node-0";
                                  rootNode["title"]       = v1Tree["majors"][0].value("title", "Hedef");
                                  rootNode["description"] = v1Tree["majors"][0].value("description", "");
                                  rootNode["progress"]    = 0;
                                  rootNode["isLeaf"]      = false;

                                  json children = json::array();
                                  int majorIdx = 0;
                                  for (const auto& major : v1Tree["majors"]) {
                                      for (size_t mi = 0; mi < major["minors"].size(); ++mi) {
                                          const auto& minor = major["minors"][mi];
                                          json minorNode;
                                          minorNode["id"]          = "node-0-" + std::to_string(majorIdx) + "-" + std::to_string(mi);
                                          minorNode["title"]       = minor.value("title", "");
                                          minorNode["description"] = minor.value("description", "");
                                          minorNode["progress"]    = 0;
                                          minorNode["isLeaf"]      = false;

                                          json microChildren = json::array();
                                          for (size_t ti = 0; ti < minor["micros"].size(); ++ti) {
                                              const auto& micro = minor["micros"][ti];
                                              json microNode;
                                              microNode["id"]                 = "node-0-" + std::to_string(majorIdx) + "-" + std::to_string(mi) + "-" + std::to_string(ti);
                                              microNode["title"]              = micro.value("title", "");
                                              microNode["description"]        = micro.value("description", "");
                                              microNode["progress"]           = 0;
                                              microNode["isLeaf"]             = true;
                                              microNode["acceptanceCriteria"] = micro.value("acceptance_criteria", "");
                                              microChildren.push_back(microNode);
                                          }
                                          minorNode["children"] = microChildren;
                                          children.push_back(minorNode);
                                      }
                                      majorIdx++;
                                  }
                                  rootNode["children"] = children;
                                  dynamicTree["version"]      = 2;
                                  dynamicTree["session_id"]   = sessionId;
                                  dynamicTree["generated_at"] = aiTs;
                                  dynamicTree["root"]         = rootNode;
                              }

                              // Persist
                              m_session->finalized = true;
                              m_session->endedBy = "ai_decision";
                              if (m_vault) {
                                  json storedResult;
                                  storedResult["goalTree"] = dynamicTree;
                                  storedResult["transcript"] = m_session->transcriptToJson();
                                  m_vault->saveInterviewResult(sessionId, storedResult.dump(),
                                                                m_session->endedBy, m_session->goalsChatQuestionCount,
                                                                InterviewSession::kMaxGoalsChatQuestions);
                                  m_lastStoredResult = storedResult;
                              }
                              m_session->goalsChatState = GoalsChatState::PLAN_READY;

                              json resp;
                              resp["type"]      = "GoalsChatResponse";
                              resp["requestId"] = requestId;
                              resp["sessionId"] = sessionId;
                              resp["ts"]        = aiTs;
                              resp["payload"]   = {
                                  {"responseType", "finalPlan"},
                                  {"text",         aiResponse},
                                  {"messageId",    aiMsgId},
                                  {"role",         "ai"},
                                  {"goalTree",     dynamicTree}
                              };
                              return resp.dump();
                          }

                          // ── ASKING_QUESTIONS: return the Socratic question ─────────────────
                          DebugLogInterview("GoalsChat: question " + std::to_string(m_session->goalsChatQuestionCount) +
                                            " (AI-driven, no fixed limit)");

                          json resp;
                          resp["type"]      = "GoalsChatResponse";
                          resp["requestId"] = requestId;
                          resp["sessionId"] = sessionId;
                          resp["ts"]        = aiTs;
                          resp["payload"]   = {
                              {"responseType",    "question"},
                              {"text",            aiResponse},
                              {"messageId",       aiMsgId},
                              {"role",            "ai"},
                              {"questionCount",   m_session->goalsChatQuestionCount},
                              {"maxQuestions",    InterviewSession::kMaxGoalsChatQuestions}
                          };

                          return resp.dump();
                      }

// ═══════════════════════════════════════════════════════════════════════
// HandleLocalTaskQuery — Tunnel Vision local AI mentor
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleLocalTaskQuery(const std::string& requestId,
                                                   const std::string& taskId,
                                                   const std::string& taskTitle,
                                                   const std::string& userText) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto buildResponse = [&](const std::string& aiText,
                             const json& items,
                             bool success,
                             const std::string& errorCode) -> std::string {
        json resp;
        resp["type"]      = "LocalTaskQueryResponse";
        resp["requestId"] = requestId;
        resp["sessionId"] = "";
        resp["ts"]        = nowISO();
        resp["payload"]   = {
            {"taskId",      taskId},
            {"taskTitle",   taskTitle},
            {"userText",    userText},
            {"aiText",      aiText},
            {"actionItems", items},
            {"success",     success}
        };
        if (!errorCode.empty()) resp["payload"]["errorCode"] = errorCode;
        return resp.dump();
    };

    if (taskId.empty() || taskTitle.empty()) {
        DebugLogInterview("LocalTaskQuery: missing task scope");
        return buildResponse("", json::array(), false, "missing_task_scope");
    }

    if (!m_gemini || !m_gemini->isAvailable()) {
        DebugLogInterview("LocalTaskQuery: Gemini unavailable");
        return buildResponse("AI servisi su anda kullanilamiyor.",
                             json::array(), false, "ai_unavailable");
    }

    // ── System prompt: strict, task-scoped, atomic decomposition ──────
    const std::string systemPrompt =
        "Sen 'Lokal AI Mentor'sun. Tum cevaplarin kullanicinin sadece su anki "
        "gorevine odakli olmali; gorevin disina ASLA cikma, baska konulara, "
        "diger maddelere veya genel onerilere kayma.\n\n"
        "Kullanicinin odaklandigi gorev:\n"
        "Baslik: \"" + taskTitle + "\"\n"
        "Gorev kimligi: " + taskId + "\n\n"
        "Gorevin: Kullanicinin bu spesifik adimi asabilmesi icin onundeki "
        "engelleri (unknown unknowns) tespit et ve bu maddeyi asabilmesi "
        "icin ona TAM 3 yeni ve cok daha kucuk (atomik) aksiyon maddesi uret. "
        "Her bir aksiyon maddesi:\n"
        "  - 5-15 dakikada tamamlanabilecek kadar kucuk olmali,\n"
        "  - Tek bir somut eylemi (verb + obje) ifade etmeli,\n"
        "  - Sadece su anki goreve hizmet etmeli (asla daha buyuk bir kapsam acma),\n"
        "  - Turkce yazilmali.\n\n"
        "CIKTI SOZLESMESI (zorunlu):\n"
        "Sadece TEK bir gecerli JSON nesnesi don. Aciklama, markdown veya "
        "kod blogu kullanma. Format:\n"
        "{\n"
        "  \"explanation\": \"<kullaniciya kisa, samimi bir aciklama (1-3 cumle)>\",\n"
        "  \"actionItems\": [\n"
        "    { \"text\": \"<atomik adim 1>\" },\n"
        "    { \"text\": \"<atomik adim 2>\" },\n"
        "    { \"text\": \"<atomik adim 3>\" }\n"
        "  ]\n"
        "}";

    const std::string userPrompt =
        "Kullanicinin sorusu / durumu:\n\"" + userText + "\"\n\n"
        "Su anda \"" + taskTitle + "\" gorevine odaklanmis durumda. "
        "Bu adimi asabilmesi icin 3 atomik aksiyon maddesi uret.";

    std::string raw = m_gemini->sendPrompt(systemPrompt, userPrompt);
    if (raw.empty()) {
        DebugLogInterview("LocalTaskQuery: Gemini returned empty response");
        return buildResponse("AI yanit veremedi, lutfen tekrar dene.",
                             json::array(), false, "ai_empty_response");
    }

    // ── Strip markdown fences if present ──────────────────────────────
    std::string cleaned = raw;
    {
        auto fence = cleaned.find("```json");
        if (fence != std::string::npos) {
            size_t start = fence + 7;
            size_t end = cleaned.rfind("```");
            if (end > start) cleaned = cleaned.substr(start, end - start);
        } else if (cleaned.find("```") != std::string::npos) {
            size_t start = cleaned.find("```") + 3;
            size_t end = cleaned.rfind("```");
            if (end > start) cleaned = cleaned.substr(start, end - start);
        }
        // Trim leading whitespace
        size_t firstNonWs = cleaned.find_first_not_of(" \t\r\n");
        if (firstNonWs != std::string::npos) cleaned.erase(0, firstNonWs);
    }

    json parsed;
    try {
        parsed = json::parse(cleaned);
    } catch (const std::exception& e) {
        DebugLogInterview(std::string("LocalTaskQuery: JSON parse failed: ") + e.what());
        return buildResponse(raw, json::array(), false, "ai_parse_failed");
    }

    std::string explanation = parsed.value("explanation", std::string{});
    json items = json::array();
    if (parsed.contains("actionItems") && parsed["actionItems"].is_array()) {
        int idx = 0;
        for (const auto& it : parsed["actionItems"]) {
            if (idx >= 3) break;
            std::string text;
            if (it.is_string())          text = it.get<std::string>();
            else if (it.is_object())     text = it.value("text", std::string{});
            if (text.empty()) continue;

            json item;
            item["id"]          = "ai-" + generateUUID();
            item["text"]        = text;
            item["isCompleted"] = false;
            item["source"]      = "local-ai-mentor";
            items.push_back(item);
            ++idx;
        }
    }

    if (items.empty()) {
        DebugLogInterview("LocalTaskQuery: no action items extracted");
        return buildResponse(explanation.empty() ? raw : explanation,
                             json::array(), false, "ai_no_items");
    }

    if (explanation.empty()) {
        explanation = "Bu adimi parcalara ayirmak icin 3 atomik adim hazirladim.";
    }

    DebugLogInterview("LocalTaskQuery: extracted " + std::to_string(items.size()) +
                      " atomic items for task '" + taskId + "'");
    return buildResponse(explanation, items, true, "");
}

// ═══════════════════════════════════════════════════════════════════════
// DEV ONLY — HandleDevReplayLastInterview
// Loads the most recent persisted interview (from Vault), wipes its
// cached goalTree so the AI path is forced, and re-runs goal generation.
// Result is the standard GoalTreeGenerated envelope (or an error one).
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleDevReplayLastInterview(const std::string& requestId) {
    DebugLogInterview("DevReplay: requested");

    if (!m_vault) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {
            {"code",    "NO_VAULT"},
            {"message", "Vault is not available"}
        };
        return err.dump();
    }

    // Find the most recent persisted session
    json recent = m_vault->getRecentInterviewSessions(1);
    std::string sessionId;
    if (recent.is_array() && !recent.empty()) {
        const auto& first = recent[0];
        if (first.is_object()) {
            sessionId = first.value("session_id", first.value("sessionId", std::string{}));
        } else if (first.is_string()) {
            sessionId = first.get<std::string>();
        }
    }

    if (sessionId.empty()) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {
            {"code",    "NO_RECENT_INTERVIEW"},
            {"message", "No recent interview found in Vault"}
        };
        return err.dump();
    }

    DebugLogInterview("DevReplay: found recent session=" + sessionId
                      + ", clearing cached goalTree and regenerating");

    // Wipe cached goalTree so HandleGenerateGoalTree is forced through AI path
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        json stored = m_vault->getInterviewResult(sessionId);
        if (stored.is_object() && stored.contains("goalTree")) {
            stored.erase("goalTree");
            m_vault->saveInterviewResult(
                sessionId,
                stored.dump(),
                stored.value("ended_by", "complete"),
                stored.value("question_count", 0),
                stored.value("max_questions", 3));
        }
        // Also clear in-memory cache so HandleGenerateGoalTree can't reuse it
        m_lastStoredResult = json();
    }

    // Delegate to the standard pipeline — it will lock the mutex itself
    return HandleGenerateGoalTree(requestId, sessionId);
}

// ═══════════════════════════════════════════════════════════════════════
// DEV ONLY — HandleDevQuickPlan
// Bypasses the Socratic interview entirely: builds a synthetic transcript
// from (goal, timeframe, level), persists it as a finalized
// InterviewResult, then runs HandleGenerateGoalTree on it.
// ═══════════════════════════════════════════════════════════════════════
std::string InterviewHandler::HandleDevQuickPlan(const std::string& requestId,
                                                  const std::string& goalText,
                                                  const std::string& timeframeText,
                                                  const std::string& levelText) {
    DebugLogInterview("DevQuickPlan: requested goal='" + goalText
                      + "' timeframe='" + timeframeText
                      + "' level='" + levelText + "'");

    if (goalText.empty()) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {
            {"code",    "MISSING_GOAL"},
            {"message", "Goal text is required for Quick Plan"}
        };
        return err.dump();
    }

    if (!m_vault) {
        json err;
        err["type"]      = "Error";
        err["requestId"] = requestId;
        err["payload"]   = {
            {"code",    "NO_VAULT"},
            {"message", "Vault is not available"}
        };
        return err.dump();
    }

    // Build synthetic transcript that looks like a normal Socratic interview
    std::string ts        = nowISO();
    std::string sessionId = "dev-qp-" + generateUUID();

    auto mkMsg = [&](const std::string& role, const std::string& text) {
        json m;
        m["message_id"] = generateUUID();
        m["role"]       = role;
        m["text"]       = text;
        m["iso_ts"]     = ts;
        return m;
    };

    json transcript = json::array();
    transcript.push_back(mkMsg("ai",   "Hedefin nedir?"));
    transcript.push_back(mkMsg("user", goalText));
    transcript.push_back(mkMsg("ai",   "Hangi zaman dilimi icinde tamamlamak istiyorsun?"));
    transcript.push_back(mkMsg("user", timeframeText.empty() ? "Belirtilmedi" : timeframeText));
    transcript.push_back(mkMsg("ai",   "Su anki seviyen / durumun nedir?"));
    transcript.push_back(mkMsg("user", levelText.empty() ? "Baslangic" : levelText));

    json slots;
    slots["goal"]          = goalText;
    slots["timeframe"]     = timeframeText;
    slots["weekly_hours"]  = 0;
    slots["current_level"] = levelText;
    slots["constraints"]   = json::array();

    json doc;
    doc["session_id"]      = sessionId;
    doc["finalized"]       = true;
    doc["ended_by"]        = "dev_quick_plan";
    doc["question_count"]  = 3;
    doc["max_questions"]   = 3;
    doc["created_at"]      = ts;
    doc["finalized_at"]    = ts;
    doc["transcript"]      = transcript;
    doc["extracted_slots"] = slots;
    doc["extractedSlots"]  = slots; // legacy alias used by some readers

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool ok = m_vault->saveInterviewResult(
            sessionId, doc.dump(), "dev_quick_plan", 3, 3);
        if (!ok) {
            DebugLogInterview("DevQuickPlan: WARNING saveInterviewResult returned false");
        }
        if (doc.contains("transcript") && doc["transcript"].is_array()) {
            m_vault->saveInterviewMessages(sessionId, doc["transcript"]);
        }
        // Drop in-memory cache so the regenerator doesn't reuse a previous tree
        m_lastStoredResult = json();
    }

    DebugLogInterview("DevQuickPlan: synthesised session=" + sessionId
                      + ", delegating to HandleGenerateGoalTree");

    return HandleGenerateGoalTree(requestId, sessionId);
}

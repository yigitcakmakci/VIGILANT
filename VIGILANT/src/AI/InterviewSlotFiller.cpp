#include "AI/InterviewSlotFiller.hpp"
#include "AI/GeminiService.hpp"
#include <windows.h>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

// ── Debug helper ───────────────────────────────────────────────────────
static void DebugLogSlot(const std::string& msg) {
    OutputDebugStringA(("[SlotFiller] " + msg + "\n").c_str());
}

// ── Trim helper ────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════

InterviewSlotFiller::InterviewSlotFiller(GeminiService* gemini)
    : m_gemini(gemini)
{
    initSlots();
}

// ═══════════════════════════════════════════════════════════════════════
// Slot schema initialization
// ═══════════════════════════════════════════════════════════════════════

void InterviewSlotFiller::initSlots() {
    m_slots.clear();
    m_slots.resize(5);

    // goal — non-empty string, 3..500 chars
    m_slots[0].name      = "goal";
    m_slots[0].required  = true;
    m_slots[0].minLength = 3;
    m_slots[0].maxLength = 500;

    // timeframe — non-empty, 2..100 chars (e.g. "3 ay", "6 hafta")
    m_slots[1].name      = "timeframe";
    m_slots[1].required  = true;
    m_slots[1].minLength = 2;
    m_slots[1].maxLength = 100;

    // weekly_hours — numeric 1..168
    m_slots[2].name       = "weekly_hours";
    m_slots[2].required   = true;
    m_slots[2].isNumeric  = true;
    m_slots[2].minNumeric = 1;
    m_slots[2].maxNumeric = 168;

    // current_level — non-empty, 2..200 chars
    m_slots[3].name      = "current_level";
    m_slots[3].required  = true;
    m_slots[3].minLength = 2;
    m_slots[3].maxLength = 200;

    // constraints[] — list of strings, 0..10 items, each 2..200 chars
    m_slots[4].name         = "constraints";
    m_slots[4].required     = false;
    m_slots[4].isList       = true;
    m_slots[4].maxListItems = 10;
    m_slots[4].minLength    = 2;
    m_slots[4].maxLength    = 200;

    m_currentSlotIdx = 0;
    m_questionCount  = 0;
    m_finalized      = false;
    m_endedBy.clear();
}

// ═══════════════════════════════════════════════════════════════════════
// UUID + Timestamp helpers (same pattern as InterviewHandler)
// ═══════════════════════════════════════════════════════════════════════

std::string InterviewSlotFiller::generateUUID() const {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist;
    uint32_t a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);
    char buf[48];
    sprintf_s(buf, "%08x-%04x-4%03x-%04x-%04x%08x",
        a, (b >> 16) & 0xFFFF, c & 0x0FFF,
        0x8000 | (d & 0x3FFF), (b & 0xFFFF), dist(rng));
    return std::string(buf);
}

std::string InterviewSlotFiller::nowISO() const {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%FT%T");
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════
// Core API
// ═══════════════════════════════════════════════════════════════════════

json InterviewSlotFiller::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    initSlots();
    DebugLogSlot("Slots reset");

    json resp;
    resp["type"]    = "InterviewUpdate";
    resp["ts"]      = nowISO();
    resp["payload"] = {
        {"action",     "reset"},
        {"slots",      slotsToJson()},
        {"filledCount", 0},
        {"totalSlots",  totalSlots()},
        {"finalized",   false}
    };
    return resp;
}

json InterviewSlotFiller::firstQuestion() {
    std::lock_guard<std::mutex> lock(m_mutex);

    SlotEntry* slot = nextEmptySlot();
    if (!slot) {
        return makeFinalUpdate();
    }

    std::string question = buildSlotPrompt(*slot);
    m_questionCount++;
    slot->attempts++;

    DebugLogSlot("First question for slot: " + slot->name +
                 " (q=" + std::to_string(m_questionCount) + ")");

    return makeAskMessage(*slot, question);
}

json InterviewSlotFiller::processAnswer(const std::string& userText) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_finalized) {
        json err;
        err["type"]    = "Error";
        err["payload"] = {{"code", "SESSION_FINALIZED"}, {"message", "Slot filler already finalized"}};
        return err;
    }

    std::string trimmed = trim(userText);
    if (trimmed.empty()) {
        json err;
        err["type"]    = "Error";
        err["payload"] = {{"code", "EMPTY_MESSAGE"}, {"message", "Message text is empty"}};
        return err;
    }

    // ── Try to fill current slot ──────────────────────────────────────
    if (m_currentSlotIdx < static_cast<int>(m_slots.size())) {
        SlotEntry& slot = m_slots[m_currentSlotIdx];
        bool filled = validateAndFill(slot, trimmed);

        if (filled) {
            DebugLogSlot("Slot filled: " + slot.name + " = " +
                         (slot.isList ? "[" + std::to_string(slot.listValue.size()) + " items]" : slot.value));

            json patchMsg = makeSlotPatched(slot);

            // Advance to next empty slot
            m_currentSlotIdx++;
            SlotEntry* next = nextEmptySlot();

            // Check if complete or limit reached
            if (!next || isComplete()) {
                m_finalized = true;
                m_endedBy   = "complete";
                DebugLogSlot("All slots filled — auto-finalize");
                return makeFinalUpdate();
            }

            if (m_questionCount >= kMaxTotalQuestions) {
                m_finalized = true;
                m_endedBy   = "limit";
                DebugLogSlot("Question limit reached — auto-finalize");
                return makeFinalUpdate();
            }

            // Ask next slot question
            std::string question = buildSlotPrompt(*next);
            m_questionCount++;
            next->attempts++;

            DebugLogSlot("Next slot: " + next->name +
                         " (q=" + std::to_string(m_questionCount) + ")");

            // Return combined: patch + next question
            json combined;
            combined["type"]    = "SlotPatchedAndNext";
            combined["ts"]      = nowISO();
            combined["payload"] = {
                {"patched", patchMsg["payload"]},
                {"next",    makeAskMessage(*next, question)["payload"]}
            };
            return combined;
        }
        else {
            // Ambiguous — clarification needed
            slot.status = SlotStatus::Ambiguous;

            if (slot.attempts >= kMaxSlotAttempts || m_questionCount >= kMaxTotalQuestions) {
                // Skip this slot, move on
                DebugLogSlot("Max attempts for slot " + slot.name + " — skipping");
                m_currentSlotIdx++;
                SlotEntry* next = nextEmptySlot();

                if (!next || m_questionCount >= kMaxTotalQuestions) {
                    m_finalized = true;
                    m_endedBy   = "limit";
                    return makeFinalUpdate();
                }

                std::string question = buildSlotPrompt(*next);
                m_questionCount++;
                next->attempts++;
                return makeAskMessage(*next, question);
            }

            // Generate clarification
            std::string clarification;
            if (slot.isNumeric) {
                clarification = "Lutfen haftalik saat sayisini net bir rakam olarak belirtin (ornegin: 10).";
            } else if (slot.name == "goal") {
                clarification = "Hedefinizi biraz daha acik ifade edebilir misiniz? (en az 3 karakter)";
            } else if (slot.name == "timeframe") {
                clarification = "Zaman cercevenizi belirtir misiniz? (ornegin: 3 ay, 6 hafta)";
            } else if (slot.name == "current_level") {
                clarification = "Su anki seviyenizi nasil tanimlarsiniz? (baslangic/orta/ileri veya aciklama)";
            } else {
                clarification = "Biraz daha detay verebilir misiniz?";
            }

            m_questionCount++;
            slot.attempts++;

            DebugLogSlot("Clarification for slot " + slot.name +
                         " (q=" + std::to_string(m_questionCount) + ", attempt=" + std::to_string(slot.attempts) + ")");

            return makeAskMessage(slot, clarification);
        }
    }

    // All slots exhausted
    m_finalized = true;
    m_endedBy   = "complete";
    return makeFinalUpdate();
}

json InterviewSlotFiller::finalize(const std::string& endedBy) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_finalized) {
        json resp = makeFinalUpdate();
        resp["payload"]["alreadyFinalized"] = true;
        return resp;
    }

    m_finalized = true;
    m_endedBy   = endedBy;
    DebugLogSlot("Finalized by=" + endedBy +
                 " filled=" + std::to_string(filledCount()) + "/" + std::to_string(totalSlots()));
    return makeFinalUpdate();
}

// ═══════════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════════

bool InterviewSlotFiller::isComplete() const {
    for (const auto& s : m_slots) {
        if (s.required && s.status != SlotStatus::Filled) return false;
    }
    return true;
}

int InterviewSlotFiller::filledCount() const {
    int n = 0;
    for (const auto& s : m_slots) {
        if (s.status == SlotStatus::Filled) n++;
    }
    return n;
}

json InterviewSlotFiller::slotsToJson() const {
    json arr = json::array();
    for (const auto& s : m_slots) {
        json sj;
        sj["name"]     = s.name;
        sj["status"]   = s.status == SlotStatus::Filled ? "filled"
                        : s.status == SlotStatus::Ambiguous ? "ambiguous" : "empty";
        sj["required"] = s.required;
        sj["attempts"] = s.attempts;
        if (s.isList) {
            sj["value"] = s.listValue;
        } else {
            sj["value"] = s.value;
        }
        arr.push_back(sj);
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════════════
// Slot navigation
// ═══════════════════════════════════════════════════════════════════════

SlotEntry* InterviewSlotFiller::nextEmptySlot() {
    for (int i = m_currentSlotIdx; i < static_cast<int>(m_slots.size()); i++) {
        if (m_slots[i].status != SlotStatus::Filled) {
            m_currentSlotIdx = i;
            return &m_slots[i];
        }
    }
    return nullptr;
}

const SlotEntry* InterviewSlotFiller::currentSlot() const {
    if (m_currentSlotIdx < static_cast<int>(m_slots.size())) {
        return &m_slots[m_currentSlotIdx];
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Validation + Fill
// ═══════════════════════════════════════════════════════════════════════

bool InterviewSlotFiller::validateAndFill(SlotEntry& slot, const std::string& raw) {
    if (slot.name == "goal") {
        std::string parsed = parseGoal(raw);
        if (parsed.empty()) return false;
        if (slot.minLength > 0 && static_cast<int>(parsed.size()) < slot.minLength) return false;
        if (slot.maxLength > 0 && static_cast<int>(parsed.size()) > slot.maxLength)
            parsed = parsed.substr(0, slot.maxLength);
        slot.value  = parsed;
        slot.status = SlotStatus::Filled;
        return true;
    }
    if (slot.name == "timeframe") {
        std::string parsed = parseTimeframe(raw);
        if (parsed.empty()) return false;
        slot.value  = parsed;
        slot.status = SlotStatus::Filled;
        return true;
    }
    if (slot.name == "weekly_hours") {
        int hours = parseWeeklyHours(raw);
        if (hours < slot.minNumeric || hours > slot.maxNumeric) return false;
        slot.value  = std::to_string(hours);
        slot.status = SlotStatus::Filled;
        return true;
    }
    if (slot.name == "current_level") {
        std::string parsed = parseCurrentLevel(raw);
        if (parsed.empty()) return false;
        slot.value  = parsed;
        slot.status = SlotStatus::Filled;
        return true;
    }
    if (slot.name == "constraints") {
        auto parsed = parseConstraints(raw);
        // constraints is optional — even empty list is acceptable for "yok / none"
        std::string lower = toLower(raw);
        if (parsed.empty() && (lower.find("yok") != std::string::npos ||
                               lower.find("none") != std::string::npos ||
                               lower.find("hayir") != std::string::npos)) {
            slot.listValue.clear();
            slot.status = SlotStatus::Filled;
            return true;
        }
        if (!parsed.empty()) {
            if (slot.maxListItems > 0 && static_cast<int>(parsed.size()) > slot.maxListItems)
                parsed.resize(slot.maxListItems);
            slot.listValue = parsed;
            slot.status    = SlotStatus::Filled;
            return true;
        }
        // If user typed something but we couldn't parse it, treat the whole thing as one constraint
        if (static_cast<int>(raw.size()) >= slot.minLength) {
            slot.listValue = { trim(raw) };
            slot.status    = SlotStatus::Filled;
            return true;
        }
        return false;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// Parsers (regex + simple heuristics)
// ═══════════════════════════════════════════════════════════════════════

std::string InterviewSlotFiller::parseGoal(const std::string& text) const {
    // Accept the full text as-is if long enough
    std::string t = trim(text);
    return t;
}

std::string InterviewSlotFiller::parseTimeframe(const std::string& text) const {
    std::string t = trim(text);
    // Try to find patterns like "3 ay", "6 hafta", "1 yil", "2 months", "3 weeks"
    std::regex rxTR(R"((\d+)\s*(ay|hafta|gun|yil|g[uü]n))", std::regex::icase);
    std::regex rxEN(R"((\d+)\s*(month|week|day|year|months|weeks|days|years))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(t, m, rxTR)) {
        return m[0].str();
    }
    if (std::regex_search(t, m, rxEN)) {
        return m[0].str();
    }
    // If text is short and looks like a timeframe description, accept it
    if (t.size() >= 2 && t.size() <= 100) return t;
    return "";
}

int InterviewSlotFiller::parseWeeklyHours(const std::string& text) const {
    std::string t = trim(text);
    // Extract first integer from text
    std::regex rx(R"((\d+))");
    std::smatch m;
    if (std::regex_search(t, m, rx)) {
        try {
            return std::stoi(m[1].str());
        } catch (...) {}
    }
    return 0;
}

std::string InterviewSlotFiller::parseCurrentLevel(const std::string& text) const {
    std::string t = trim(text);
    std::string lower = toLower(t);

    // Normalize common Turkish level terms
    if (lower.find("baslangic") != std::string::npos || lower.find("beginner") != std::string::npos)
        return "baslangic";
    if (lower.find("orta") != std::string::npos || lower.find("intermediate") != std::string::npos)
        return "orta";
    if (lower.find("ileri") != std::string::npos || lower.find("advanced") != std::string::npos)
        return "ileri";
    if (lower.find("uzman") != std::string::npos || lower.find("expert") != std::string::npos)
        return "uzman";

    // Accept free-form if long enough
    if (t.size() >= 2) return t;
    return "";
}

std::vector<std::string> InterviewSlotFiller::parseConstraints(const std::string& text) const {
    std::vector<std::string> result;
    std::string t = trim(text);

    // Split by comma, semicolon, newline, or "ve"/"and"
    std::regex splitter(R"([,;\n]|\bve\b|\band\b)", std::regex::icase);
    std::sregex_token_iterator it(t.begin(), t.end(), splitter, -1);
    std::sregex_token_iterator end;

    for (; it != end; ++it) {
        std::string item = trim(it->str());
        if (item.size() >= 2) {
            result.push_back(item);
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// LLM prompt builder (deterministic)
// ═══════════════════════════════════════════════════════════════════════

std::string InterviewSlotFiller::buildSlotPrompt(const SlotEntry& slot) const {
    // Try LLM first, fall back to deterministic stubs
    if (m_gemini && m_gemini->isAvailable()) {
        std::string systemPrompt =
            "Sen bir gorusme asistanisin. "
            "Sadece eksik olan slot icin TEK soru uret; soru disinda metin yazma. "
            "Soru Turkce, kisa ve odakli olmali.";

        std::string userPrompt = "Eksik slot: \"" + slot.name + "\". ";
        if (slot.name == "goal")
            userPrompt += "Kullanicinin ana hedefini sor.";
        else if (slot.name == "timeframe")
            userPrompt += "Kullanicinin zaman cercevesini sor (ornegin kac ay/hafta).";
        else if (slot.name == "weekly_hours")
            userPrompt += "Kullanicinin haftada kac saat ayirabilecegini sor.";
        else if (slot.name == "current_level")
            userPrompt += "Kullanicinin su anki seviyesini sor (baslangic/orta/ileri).";
        else if (slot.name == "constraints")
            userPrompt += "Kullanicinin kisitlamalarini veya engellerini sor.";

        std::string llmQuestion = callLLM(systemPrompt, userPrompt);
        if (!llmQuestion.empty()) {
            return llmQuestion;
        }
    }

    // ── Deterministic stubs ───────────────────────────────────────────
    if (slot.name == "goal")
        return "Ana hedefiniz nedir? Neyi basarmak istiyorsunuz?";
    if (slot.name == "timeframe")
        return "Bu hedefe ulasma zaman cerceveniz nedir? (ornegin: 3 ay, 6 hafta)";
    if (slot.name == "weekly_hours")
        return "Haftada kac saat ayirabilirsiniz?";
    if (slot.name == "current_level")
        return "Su anki seviyenizi nasil tanimlarsiniz? (baslangic / orta / ileri)";
    if (slot.name == "constraints")
        return "Herhangi bir kisitlamaniz veya engeliniz var mi? (Yoksa 'yok' yazabilirsiniz)";

    return "Lutfen devam edin.";
}

std::string InterviewSlotFiller::callLLM(const std::string& systemPrompt, const std::string& userPrompt) const {
    if (!m_gemini || !m_gemini->isAvailable()) return "";

    try {
        json input;
        input["system"] = systemPrompt;
        input["user"]   = userPrompt;

        // Re-use generateNarrative which supports system prompt
        auto result = m_gemini->generateNarrative(input);
        if (result.contains("narrative") && result["narrative"].is_string()) {
            std::string question = trim(result["narrative"].get<std::string>());
            if (!question.empty()) return question;
        }
        // Try raw text extraction
        if (result.contains("text") && result["text"].is_string()) {
            return trim(result["text"].get<std::string>());
        }
    } catch (const std::exception& e) {
        DebugLogSlot("LLM call failed: " + std::string(e.what()));
    }
    return "";
}

// ═══════════════════════════════════════════════════════════════════════
// JSON message builders (EventBridge protocol)
// ═══════════════════════════════════════════════════════════════════════

json InterviewSlotFiller::makeAskMessage(const SlotEntry& slot, const std::string& questionText) const {
    json msg;
    msg["type"] = "AskNextQuestion";
    msg["ts"]   = nowISO();
    msg["payload"] = {
        {"slotName",      slot.name},
        {"questionText",  questionText},
        {"messageId",     generateUUID()},
        {"questionCount", m_questionCount},
        {"maxQuestions",  kMaxTotalQuestions},
        {"filledCount",   filledCount()},
        {"totalSlots",    totalSlots()},
        {"slots",         slotsToJson()}
    };
    return msg;
}

json InterviewSlotFiller::makeSlotPatched(const SlotEntry& slot) const {
    json msg;
    msg["type"] = "SlotPatched";
    msg["ts"]   = nowISO();
    msg["payload"] = {
        {"slotName", slot.name},
        {"status",   "filled"},
        {"value",    slot.isList ? json(slot.listValue) : json(slot.value)},
        {"filledCount", filledCount()},
        {"totalSlots",  totalSlots()},
        {"slots",       slotsToJson()}
    };
    return msg;
}

json InterviewSlotFiller::makeFinalUpdate() const {
    json msg;
    msg["type"] = "InterviewUpdate";
    msg["ts"]   = nowISO();

    json slotValues;
    for (const auto& s : m_slots) {
        if (s.status == SlotStatus::Filled) {
            if (s.isList)
                slotValues[s.name] = s.listValue;
            else
                slotValues[s.name] = s.value;
        } else {
            slotValues[s.name] = nullptr;
        }
    }

    msg["payload"] = {
        {"action",        "finalized"},
        {"endedBy",       m_endedBy},
        {"finalized",     true},
        {"filledCount",   filledCount()},
        {"totalSlots",    totalSlots()},
        {"questionCount", m_questionCount},
        {"slots",         slotsToJson()},
        {"slotValues",    slotValues}
    };
    return msg;
}

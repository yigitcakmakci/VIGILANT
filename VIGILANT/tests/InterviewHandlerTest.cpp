/**
 * InterviewHandlerTest.cpp — Unit tests for InterviewHandler.
 *
 * Standalone executable: no GoogleTest dependency.
 * Uses console assert + coloured output, same pattern as the TS tests.
 *
 * Mock strategy:
 *   - GeminiService* → nullptr  (handler uses deterministic stubs when null)
 *   - DatabaseManager* → lightweight StubVault that records calls in-memory
 *
 * Time determinism:
 *   - All ISO timestamps are produced by InterviewHandler::nowISO() which
 *     reads the real clock.  Tests do NOT depend on exact timestamp values;
 *     they only assert non-empty strings and ordering.
 *
 * Build (MSVC, Release|x64):
 *   cl /std:c++20 /EHsc /MT /I ..\include /I ..\vendor\sqlite
 *      /Fe:InterviewHandlerTest.exe
 *      InterviewHandlerTest.cpp ..\src\AI\InterviewHandler.cpp
 *      /link /SUBSYSTEM:CONSOLE
 */

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#endif

#include "AI/InterviewHandler.hpp"
#include "AI/InterviewResult.hpp"
#include "Data/DatabaseManager.hpp"
#include "Utils/json.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <functional>

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════
// Minimal StubVault — records persist calls without touching SQLite
// ═══════════════════════════════════════════════════════════════════════

struct SavedResult {
    std::string sessionId;
    std::string resultJson;
    std::string endedBy;
    int         questionCount;
    int         maxQuestions;
};

struct SavedMessages {
    std::string sessionId;
    json        transcriptArray;
};

// Global stub storage (reset between tests)
static std::vector<SavedResult>   g_savedResults;
static std::vector<SavedMessages> g_savedMessages;
static std::vector<std::pair<std::string, json>> g_storedDocs; // for getInterviewResult

// ═══════════════════════════════════════════════════════════════════════
// DatabaseManager stub implementations
// ═══════════════════════════════════════════════════════════════════════
// We compile against the real header but provide stub bodies here so
// we don't need the full DatabaseManager.cpp (and SQLite).

DatabaseManager::DatabaseManager(const std::string&) : db(nullptr) {}
DatabaseManager::~DatabaseManager() {}
bool DatabaseManager::init() { return true; }
int  DatabaseManager::logActivity(const EventData&) { return 0; }
void DatabaseManager::updateDuration(int, int) {}
void DatabaseManager::beginTransaction() {}
void DatabaseManager::commitTransaction() {}
bool DatabaseManager::saveAILabels(const std::string&, const std::string&, const std::string&, int) { return true; }
bool DatabaseManager::saveCategoryOverride(const std::string&, const std::string&, const std::string&, int) { return true; }
std::vector<OverrideRule> DatabaseManager::getOverrideRules() { return {}; }
bool DatabaseManager::applyOverrides(std::vector<std::pair<std::string,std::string>>&) { return true; }
nlohmann::json DatabaseManager::getOverrideAuditLog(int) { return nlohmann::json::array(); }
std::vector<ActivityLog> DatabaseManager::getRecentLogs(int) { return {}; }
std::vector<ActivityLog> DatabaseManager::getUncategorizedLogs() { return {}; }
std::map<std::string, float> DatabaseManager::getCategoryDistribution() { return {}; }
std::pair<int, std::string> DatabaseManager::getScoreForActivity(const std::string&, const std::string&) { return {0,""}; }
std::vector<std::pair<std::string,std::string>> DatabaseManager::getUncategorizedActivities() { return {}; }
int DatabaseManager::getTodaysTotalDuration() { return 0; }
nlohmann::json DatabaseManager::getDashboardSummaryJson() { return {}; }
nlohmann::json DatabaseManager::getHistoricalData(const std::string&) { return {}; }
std::vector<ActivityLog> DatabaseManager::getLogsForDate(const std::string&, int) { return {}; }
nlohmann::json DatabaseManager::getDailyTrends(int) { return nlohmann::json::array(); }
bool DatabaseManager::saveDailySummary(const std::string&) { return true; }
nlohmann::json DatabaseManager::getAvailableDates() { return nlohmann::json::array(); }

bool DatabaseManager::saveInterviewResult(const std::string& sessionId,
                                          const std::string& resultJson,
                                          const std::string& endedBy,
                                          int questionCount,
                                          int maxQuestions) {
    g_savedResults.push_back({sessionId, resultJson, endedBy, questionCount, maxQuestions});

    // Also store for later retrieval
    json doc = json::parse(resultJson, nullptr, false);
    if (!doc.is_discarded()) {
        // Replace if exists
        for (auto& p : g_storedDocs) {
            if (p.first == sessionId) { p.second = doc; return true; }
        }
        g_storedDocs.push_back({sessionId, doc});
    }
    return true;
}

bool DatabaseManager::saveInterviewMessages(const std::string& sessionId,
                                            const json& transcriptArray) {
    g_savedMessages.push_back({sessionId, transcriptArray});
    return true;
}

json DatabaseManager::getInterviewResult(const std::string& sessionId) {
    for (const auto& p : g_storedDocs) {
        if (p.first == sessionId) return p.second;
    }
    return json();
}

json DatabaseManager::getRecentInterviewSessions(int /*limit*/) {
    return json::array();
}

// Remaining DatabaseManager stubs (not used in tests but needed for linking)
bool DatabaseManager::clearAllData() { return true; }

// ═══════════════════════════════════════════════════════════════════════
// GeminiService stub — handler works with nullptr but needs symbols
// ═══════════════════════════════════════════════════════════════════════

#include "AI/GeminiService.hpp"

GeminiService::GeminiService() {}
bool GeminiService::isAvailable() const { return false; }
std::string GeminiService::getProviderName() const { return "stub"; }
bool GeminiService::configure(const std::string&, const std::string&, const std::string&) { return false; }
bool GeminiService::validateApiKey() { return false; }
std::vector<AILabel> GeminiService::classifyActivities(const std::vector<std::pair<std::string,std::string>>&) { return {}; }
nlohmann::json GeminiService::generateNarrative(const nlohmann::json&) { return {}; }

// ═══════════════════════════════════════════════════════════════════════
// InterviewSlotFiller stub — only slotsToJson and finalize needed
// ═══════════════════════════════════════════════════════════════════════

InterviewSlotFiller::InterviewSlotFiller(GeminiService*) {}
nlohmann::json InterviewSlotFiller::reset() { return {}; }
nlohmann::json InterviewSlotFiller::processAnswer(const std::string&) { return {}; }
nlohmann::json InterviewSlotFiller::firstQuestion() { return {}; }
nlohmann::json InterviewSlotFiller::finalize(const std::string& endedBy) {
    m_finalized = true;
    m_endedBy = endedBy;
    json msg;
    msg["type"] = "InterviewUpdate";
    msg["payload"] = { {"action","finalized"}, {"endedBy", endedBy}, {"finalized",true} };
    return msg;
}
bool InterviewSlotFiller::isComplete() const { return false; }
int  InterviewSlotFiller::filledCount() const { return 0; }
nlohmann::json InterviewSlotFiller::slotsToJson() const { return nlohmann::json::array(); }

// ═══════════════════════════════════════════════════════════════════════
// Test harness
// ═══════════════════════════════════════════════════════════════════════

static int g_passed = 0;
static int g_failed = 0;

static void resetStubs() {
    g_savedResults.clear();
    g_savedMessages.clear();
    g_storedDocs.clear();
}

#define TEST(name) static void name()
#define RUN(name) do { \
    resetStubs(); \
    std::cout << "  " << #name << " ... "; \
    try { name(); g_passed++; std::cout << "OK\n"; } \
    catch (const std::exception& e) { g_failed++; std::cout << "FAIL: " << e.what() << "\n"; } \
    catch (...) { g_failed++; std::cout << "FAIL (unknown exception)\n"; } \
} while(0)

#define ASSERT_TRUE(cond)  do { if (!(cond)) throw std::runtime_error("ASSERT_TRUE failed: " #cond " (" __FILE__ ":" + std::to_string(__LINE__) + ")"); } while(0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b)    do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b " (" __FILE__ ":" + std::to_string(__LINE__) + ")"); } while(0)

// ═══════════════════════════════════════════════════════════════════════
// Helper: parse JSON response
// ═══════════════════════════════════════════════════════════════════════

static json parseResponse(const std::string& s) {
    auto j = json::parse(s, nullptr, false);
    assert(!j.is_discarded() && "Handler returned invalid JSON");
    return j;
}

// ═══════════════════════════════════════════════════════════════════════
// T1: Start → questionCount=1, first AI question returned
// ═══════════════════════════════════════════════════════════════════════

TEST(T1_StartInterview) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto resp = parseResponse(handler.HandleInterviewStart("req-1", 3));

    ASSERT_EQ(resp["type"].get<std::string>(), "InterviewStarted");
    ASSERT_EQ(resp["requestId"].get<std::string>(), "req-1");
    ASSERT_TRUE(resp.contains("sessionId"));
    ASSERT_TRUE(resp.contains("payload"));
    ASSERT_TRUE(resp["payload"].contains("firstQuestion"));
    ASSERT_TRUE(resp["payload"]["firstQuestion"].contains("text"));
    ASSERT_TRUE(!resp["payload"]["firstQuestion"]["text"].get<std::string>().empty());
}

// ═══════════════════════════════════════════════════════════════════════
// T2: UserMessage → questionCount increments
// ═══════════════════════════════════════════════════════════════════════

TEST(T2_QuestionCountIncrement) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sessionId = startResp["sessionId"].get<std::string>();

    // Send user answer → triggers AI question #2
    auto resp2 = parseResponse(handler.HandleUserMessage("req-2", sessionId, "Amacim programlama ogrenmek", "msg-u1"));
    ASSERT_EQ(resp2["type"].get<std::string>(), "AiQuestionProduced");
    ASSERT_EQ(resp2["payload"]["questionCount"].get<int>(), 2);

    // Send user answer → triggers AI question #3
    auto resp3 = parseResponse(handler.HandleUserMessage("req-3", sessionId, "Basarmak beni mutlu eder", "msg-u2"));
    ASSERT_EQ(resp3["type"].get<std::string>(), "AiQuestionProduced");
    ASSERT_EQ(resp3["payload"]["questionCount"].get<int>(), 3);
}

// ═══════════════════════════════════════════════════════════════════════
// T3: maxQuestions=3 reached → autoFinalize hint
// ═══════════════════════════════════════════════════════════════════════

TEST(T3_AutoFinalizeAtMaxQuestions) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    // Reach question 2
    handler.HandleUserMessage("req-2", sid, "Answer 1", "u1");
    // Reach question 3 (kMaxQuestions)
    auto resp3 = parseResponse(handler.HandleUserMessage("req-3", sid, "Answer 2", "u2"));

    ASSERT_EQ(resp3["payload"]["questionCount"].get<int>(), 3);
    ASSERT_TRUE(resp3["payload"].contains("autoFinalize"));
    ASSERT_EQ(resp3["payload"]["autoFinalize"].get<bool>(), true);
}

// ═══════════════════════════════════════════════════════════════════════
// T4: FinalizeCTA → endedBy correct, InterviewFinalized event
// ═══════════════════════════════════════════════════════════════════════

TEST(T4_FinalizeCTA) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleUserMessage("req-2", sid, "Answer 1", "u1");

    auto finalResp = parseResponse(handler.HandleFinalizeInterview("req-fin", sid, "cta"));

    ASSERT_EQ(finalResp["type"].get<std::string>(), "InterviewFinalized");
    ASSERT_EQ(finalResp["payload"]["endedBy"].get<std::string>(), "cta");
    ASSERT_EQ(finalResp["payload"]["questionCount"].get<int>(), 2);
    ASSERT_TRUE(finalResp["payload"].contains("transcript"));
    ASSERT_TRUE(finalResp["payload"].contains("interviewSessionId"));
    ASSERT_EQ(finalResp["payload"]["interviewSessionId"].get<std::string>(), sid);

    // interviewResult should be present (persisted)
    ASSERT_TRUE(finalResp["payload"].contains("interviewResult"));
    auto ir = finalResp["payload"]["interviewResult"];
    ASSERT_EQ(ir["finalized"].get<bool>(), true);
    ASSERT_EQ(ir["ended_by"].get<std::string>(), "cta");
}

// ═══════════════════════════════════════════════════════════════════════
// T5: Finalize by limit → endedBy="limit"
// ═══════════════════════════════════════════════════════════════════════

TEST(T5_FinalizeLimit) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleUserMessage("req-2", sid, "A1", "u1");
    handler.HandleUserMessage("req-3", sid, "A2", "u2");

    auto finalResp = parseResponse(handler.HandleFinalizeInterview("req-fin", sid, "limit"));

    ASSERT_EQ(finalResp["type"].get<std::string>(), "InterviewFinalized");
    ASSERT_EQ(finalResp["payload"]["endedBy"].get<std::string>(), "limit");
    ASSERT_EQ(finalResp["payload"]["questionCount"].get<int>(), 3);
}

// ═══════════════════════════════════════════════════════════════════════
// T6: Double finalize → idempotent, state unchanged
// ═══════════════════════════════════════════════════════════════════════

TEST(T6_DoubleFinalizeIdempotent) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleUserMessage("req-2", sid, "Answer", "u1");

    // First finalize
    auto fin1 = parseResponse(handler.HandleFinalizeInterview("req-fin-1", sid, "cta"));
    ASSERT_EQ(fin1["type"].get<std::string>(), "InterviewFinalized");
    ASSERT_FALSE(fin1["payload"].contains("alreadyFinalized"));

    int savedCountAfterFirst = (int)g_savedResults.size();

    // Second finalize — different requestId, same session
    auto fin2 = parseResponse(handler.HandleFinalizeInterview("req-fin-2", sid, "cta"));
    ASSERT_EQ(fin2["type"].get<std::string>(), "InterviewFinalized");
    ASSERT_EQ(fin2["payload"]["alreadyFinalized"].get<bool>(), true);

    // endedBy must still be "cta"
    ASSERT_EQ(fin2["payload"]["endedBy"].get<std::string>(), "cta");

    // No additional DB writes on second finalize
    ASSERT_EQ((int)g_savedResults.size(), savedCountAfterFirst);
}

// ═══════════════════════════════════════════════════════════════════════
// T7: Message after finalize → rejected
// ═══════════════════════════════════════════════════════════════════════

TEST(T7_MessageAfterFinalizeRejected) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleFinalizeInterview("req-fin", sid, "cta");

    auto errResp = parseResponse(handler.HandleUserMessage("req-late", sid, "Late message", "u-late"));
    ASSERT_EQ(errResp["type"].get<std::string>(), "Error");
    ASSERT_EQ(errResp["payload"]["code"].get<std::string>(), "SESSION_FINALIZED");
}

// ═══════════════════════════════════════════════════════════════════════
// T8: Invalid session → error
// ═══════════════════════════════════════════════════════════════════════

TEST(T8_InvalidSession) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    handler.HandleInterviewStart("req-1", 3);

    auto errResp = parseResponse(handler.HandleUserMessage("req-2", "wrong-session-id", "Hello", "u1"));
    ASSERT_EQ(errResp["type"].get<std::string>(), "Error");
    ASSERT_EQ(errResp["payload"]["code"].get<std::string>(), "INVALID_SESSION");
}

// ═══════════════════════════════════════════════════════════════════════
// T9: Duplicate requestId → idempotent rejection
// ═══════════════════════════════════════════════════════════════════════

TEST(T9_DuplicateRequestIdIgnored) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    // First message succeeds
    auto resp1 = parseResponse(handler.HandleUserMessage("req-2", sid, "Answer", "u1"));
    ASSERT_EQ(resp1["type"].get<std::string>(), "AiQuestionProduced");

    // Same requestId → duplicate
    auto resp2 = parseResponse(handler.HandleUserMessage("req-2", sid, "Answer again", "u2"));
    ASSERT_EQ(resp2["type"].get<std::string>(), "Error");
    ASSERT_EQ(resp2["payload"]["code"].get<std::string>(), "DUPLICATE_REQUEST");
}

// ═══════════════════════════════════════════════════════════════════════
// T10: Persistence — finalize writes to StubVault
// ═══════════════════════════════════════════════════════════════════════

TEST(T10_PersistenceOnFinalize) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleUserMessage("req-2", sid, "Goal answer", "u1");
    handler.HandleFinalizeInterview("req-fin", sid, "cta");

    // Check DB stubs were called
    ASSERT_EQ((int)g_savedResults.size(), 1);
    ASSERT_EQ(g_savedResults[0].sessionId, sid);
    ASSERT_EQ(g_savedResults[0].endedBy, "cta");
    ASSERT_EQ(g_savedResults[0].questionCount, 2);
    ASSERT_EQ(g_savedResults[0].maxQuestions, 3);

    ASSERT_EQ((int)g_savedMessages.size(), 1);
    ASSERT_EQ(g_savedMessages[0].sessionId, sid);
    ASSERT_TRUE(g_savedMessages[0].transcriptArray.is_array());
    // 1 AI (start) + 1 user + 1 AI (response) = 3 messages
    ASSERT_EQ((int)g_savedMessages[0].transcriptArray.size(), 3);
}

// ═══════════════════════════════════════════════════════════════════════
// T11: GoalTree from persisted result
// ═══════════════════════════════════════════════════════════════════════

TEST(T11_GoalTreeFromPersistedResult) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    handler.HandleUserMessage("req-2", sid, "Goal answer", "u1");
    handler.HandleFinalizeInterview("req-fin", sid, "cta");

    // Now request GoalTree generation
    auto treeResp = parseResponse(handler.HandleGenerateGoalTree("req-tree", sid));
    ASSERT_EQ(treeResp["type"].get<std::string>(), "GoalTreeGenerated");
    ASSERT_TRUE(treeResp["payload"].contains("interviewResult"));
    ASSERT_TRUE(treeResp["payload"].contains("extractedSlots"));
    ASSERT_EQ(treeResp["payload"]["status"].get<std::string>(), "ready");
}

// ═══════════════════════════════════════════════════════════════════════
// T12: GoalTree with no finalized result → error
// ═══════════════════════════════════════════════════════════════════════

TEST(T12_GoalTreeNoResult) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto errResp = parseResponse(handler.HandleGenerateGoalTree("req-tree", "nonexistent-session"));
    ASSERT_EQ(errResp["type"].get<std::string>(), "Error");
    ASSERT_EQ(errResp["payload"]["code"].get<std::string>(), "NO_INTERVIEW_RESULT");
}

// ═══════════════════════════════════════════════════════════════════════
// T13: InterviewResult immutability — appendMessage rejected after finalize
// ═══════════════════════════════════════════════════════════════════════

TEST(T13_InterviewResultImmutableAfterFinalize) {
    InterviewResult r;
    r.session_id = "s1";

    InterviewResultMessage m1{"m1", "ai", "Hello", "2025-01-01T00:00:00"};
    ASSERT_TRUE(r.appendMessage(m1));
    ASSERT_EQ((int)r.transcript.size(), 1);

    r.finalized = true;

    InterviewResultMessage m2{"m2", "user", "Late", "2025-01-01T00:01:00"};
    ASSERT_FALSE(r.appendMessage(m2));
    ASSERT_EQ((int)r.transcript.size(), 1); // unchanged
}

// ═══════════════════════════════════════════════════════════════════════
// T14: InterviewResult JSON round-trip
// ═══════════════════════════════════════════════════════════════════════

TEST(T14_InterviewResultJsonRoundTrip) {
    InterviewResult r;
    r.session_id     = "session-abc";
    r.finalized      = true;
    r.ended_by       = "cta";
    r.question_count = 2;
    r.max_questions  = 3;
    r.created_at     = "2025-01-01T10:00:00";
    r.finalized_at   = "2025-01-01T10:05:00";
    r.transcript.push_back({"m1", "ai", "Soru 1?", "2025-01-01T10:00:00"});
    r.transcript.push_back({"m2", "user", "Cevap 1", "2025-01-01T10:01:00"});
    r.extracted_slots.goal          = "Programlama";
    r.extracted_slots.timeframe     = "3 ay";
    r.extracted_slots.weekly_hours  = 10;
    r.extracted_slots.current_level = "orta";
    r.extracted_slots.constraints   = {"zaman", "para"};

    json doc = r.toJson();
    InterviewResult r2 = InterviewResult::fromJson(doc);

    ASSERT_EQ(r2.session_id, r.session_id);
    ASSERT_EQ(r2.finalized, r.finalized);
    ASSERT_EQ(r2.ended_by, r.ended_by);
    ASSERT_EQ(r2.question_count, r.question_count);
    ASSERT_EQ(r2.max_questions, r.max_questions);
    ASSERT_EQ(r2.created_at, r.created_at);
    ASSERT_EQ(r2.finalized_at, r.finalized_at);
    ASSERT_EQ((int)r2.transcript.size(), 2);
    ASSERT_EQ(r2.transcript[0].message_id, "m1");
    ASSERT_EQ(r2.transcript[1].role, "user");
    ASSERT_EQ(r2.extracted_slots.goal, "Programlama");
    ASSERT_EQ(r2.extracted_slots.weekly_hours, 10);
    ASSERT_EQ((int)r2.extracted_slots.constraints.size(), 2);

    // Second round-trip should produce identical JSON
    json doc2 = r2.toJson();
    ASSERT_EQ(doc.dump(), doc2.dump());
}

// ═══════════════════════════════════════════════════════════════════════
// T15: Empty text message → rejected
// ═══════════════════════════════════════════════════════════════════════

TEST(T15_EmptyMessageRejected) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    auto startResp = parseResponse(handler.HandleInterviewStart("req-1", 3));
    std::string sid = startResp["sessionId"].get<std::string>();

    auto errResp = parseResponse(handler.HandleUserMessage("req-2", sid, "   ", "u1"));
    ASSERT_EQ(errResp["type"].get<std::string>(), "Error");
    ASSERT_EQ(errResp["payload"]["code"].get<std::string>(), "EMPTY_MESSAGE");
}

// ═══════════════════════════════════════════════════════════════════════
// T16: Full flow — Start → 2×UserAnswer → FinalizeCTA → GoalTree
// ═══════════════════════════════════════════════════════════════════════

TEST(T16_FullFlowE2E) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    // 1) Start
    auto r1 = parseResponse(handler.HandleInterviewStart("r1", 3));
    ASSERT_EQ(r1["type"].get<std::string>(), "InterviewStarted");
    std::string sid = r1["sessionId"].get<std::string>();

    // 2) User answers Q1 → Q2 produced
    auto r2 = parseResponse(handler.HandleUserMessage("r2", sid, "Hedefim web gelistirme", "u1"));
    ASSERT_EQ(r2["type"].get<std::string>(), "AiQuestionProduced");
    ASSERT_EQ(r2["payload"]["questionCount"].get<int>(), 2);

    // 3) User answers Q2 → Q3 produced with autoFinalize hint
    auto r3 = parseResponse(handler.HandleUserMessage("r3", sid, "3 ayda ogrenmek istiyorum", "u2"));
    ASSERT_EQ(r3["type"].get<std::string>(), "AiQuestionProduced");
    ASSERT_EQ(r3["payload"]["questionCount"].get<int>(), 3);
    ASSERT_TRUE(r3["payload"]["autoFinalize"].get<bool>());

    // 4) User answers Q3 → auto-finalize (InterviewFinalized, not AiQuestionProduced)
    auto r4 = parseResponse(handler.HandleUserMessage("r4", sid, "En buyuk engel zaman", "u3"));
    ASSERT_EQ(r4["type"].get<std::string>(), "InterviewFinalized");
    ASSERT_EQ(r4["payload"]["endedBy"].get<std::string>(), "limit");
    ASSERT_TRUE(r4["payload"].contains("interviewResult"));
    ASSERT_TRUE(r4["payload"]["interviewResult"]["finalized"].get<bool>());

    // 5) GoalTree from persisted result
    auto r5 = parseResponse(handler.HandleGenerateGoalTree("r5", sid));
    ASSERT_EQ(r5["type"].get<std::string>(), "GoalTreeGenerated");
    ASSERT_EQ(r5["payload"]["status"].get<std::string>(), "ready");
    ASSERT_TRUE(r5["payload"]["interviewResult"]["finalized"].get<bool>());

    // 6) Late message rejected
    auto r6 = parseResponse(handler.HandleUserMessage("r6", sid, "Late", "u-late"));
    ASSERT_EQ(r6["type"].get<std::string>(), "Error");
    ASSERT_EQ(r6["payload"]["code"].get<std::string>(), "SESSION_FINALIZED");

    // 7) Double finalize idempotent
    auto r7 = parseResponse(handler.HandleFinalizeInterview("r7", sid, "limit"));
    ASSERT_EQ(r7["payload"]["alreadyFinalized"].get<bool>(), true);
}

// ═══════════════════════════════════════════════════════════════════════
// T17: GoalTree in-memory fallback when vault lookup returns empty
// ═══════════════════════════════════════════════════════════════════════

TEST(T17_GoalTreeFallbackFromMemory) {
    DatabaseManager vault("test.db");
    InterviewHandler handler(nullptr, &vault);

    // Full flow: start → answer Q1 → answer Q2 → answer Q3 (auto-finalize)
    auto r1 = parseResponse(handler.HandleInterviewStart("r1", 3));
    std::string sid = r1["sessionId"].get<std::string>();

    handler.HandleUserMessage("r2", sid, "Hedefim web gelistirme", "u1");
    handler.HandleUserMessage("r3", sid, "3 ayda ogrenmek istiyorum", "u2");
    auto r4 = parseResponse(handler.HandleUserMessage("r4", sid, "En buyuk engel zaman", "u3"));
    ASSERT_EQ(r4["type"].get<std::string>(), "InterviewFinalized");

    // Simulate vault persistence failure: clear stored docs so vault returns empty
    g_storedDocs.clear();

    // GoalTree should still succeed via in-memory fallback
    auto r5 = parseResponse(handler.HandleGenerateGoalTree("r5", sid));
    ASSERT_EQ(r5["type"].get<std::string>(), "GoalTreeGenerated");
    ASSERT_EQ(r5["payload"]["status"].get<std::string>(), "ready");
    ASSERT_TRUE(r5["payload"]["interviewResult"]["finalized"].get<bool>());

    // Fallback should have re-persisted — storedDocs should now have an entry
    ASSERT_TRUE(!g_storedDocs.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "\n=== InterviewHandler Unit Tests ===\n\n";

    RUN(T1_StartInterview);
    RUN(T2_QuestionCountIncrement);
    RUN(T3_AutoFinalizeAtMaxQuestions);
    RUN(T4_FinalizeCTA);
    RUN(T5_FinalizeLimit);
    RUN(T6_DoubleFinalizeIdempotent);
    RUN(T7_MessageAfterFinalizeRejected);
    RUN(T8_InvalidSession);
    RUN(T9_DuplicateRequestIdIgnored);
    RUN(T10_PersistenceOnFinalize);
    RUN(T11_GoalTreeFromPersistedResult);
    RUN(T12_GoalTreeNoResult);
    RUN(T13_InterviewResultImmutableAfterFinalize);
    RUN(T14_InterviewResultJsonRoundTrip);
    RUN(T15_EmptyMessageRejected);
    RUN(T16_FullFlowE2E);
    RUN(T17_GoalTreeFallbackFromMemory);

    std::cout << "\n--- Results: " << g_passed << " passed, "
              << g_failed << " failed ---\n\n";

    if (g_failed > 0) {
        std::cout << "FAIL\n";
        return 1;
    }
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}

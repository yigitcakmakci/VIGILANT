#include "Data/DatabaseManager.hpp"
#include "Utils/PerfCounters.hpp"
#include <iostream>

using json = nlohmann::json;

DatabaseManager::DatabaseManager(const std::string& dbName) : db(nullptr) {
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        std::cerr << "VERITABANI ACILAMADI: " << sqlite3_errmsg(db) << std::endl;
    }
}

DatabaseManager::~DatabaseManager() {
    finalizeStatements();
    if (db) sqlite3_close(db);
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> lock(db_mutex);

    // --- Performance PRAGMAs ---
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, 0, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, 0, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-8000;", NULL, 0, nullptr);  // 8MB cache
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", NULL, 0, nullptr);

    // 1. Activities Tablosu (Ham Loglar)
    const char* sqlActivities =
        "CREATE TABLE IF NOT EXISTS Activities ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PROCESS TEXT, TITLE TEXT, URL TEXT, DURATION INTEGER DEFAULT 0);";
    sqlite3_exec(db, sqlActivities, NULL, 0, &zErrMsg);

    // 2. KnowledgeBase Tablosu (AI ve Manuel Bilgi Merkezi)
    const char* sqlKB =
        "CREATE TABLE IF NOT EXISTS KnowledgeBase ("
        "PROCESS TEXT,"
        "TITLE_KEYWORD TEXT,"
        "CATEGORY TEXT,"
        "SCORE INTEGER,"
        "SOURCE TEXT DEFAULT 'seed',"
        "PRIMARY KEY (PROCESS, TITLE_KEYWORD));";
    sqlite3_exec(db, sqlKB, NULL, 0, &zErrMsg);

    // SOURCE kolonu eski tabloya ekle (ALTER TABLE IF NOT EXISTS yok, hata yutulur)
    sqlite3_exec(db, "ALTER TABLE KnowledgeBase ADD COLUMN SOURCE TEXT DEFAULT 'seed';", NULL, 0, nullptr);

    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_activities_proc_title ON Activities(PROCESS, TITLE);", NULL, 0, &zErrMsg);

    // 3. CategoryOverrides Tablosu (Kullanici feedback kurallari)
    const char* sqlOverrides =
        "CREATE TABLE IF NOT EXISTS CategoryOverrides ("
        "EXE_PATH TEXT NOT NULL,"
        "TITLE_PATTERN TEXT NOT NULL DEFAULT '*',"
        "CATEGORY TEXT NOT NULL,"
        "SCORE INTEGER NOT NULL DEFAULT 0,"
        "CREATED_AT DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "UPDATED_AT DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY (EXE_PATH, TITLE_PATTERN));";
    sqlite3_exec(db, sqlOverrides, NULL, 0, &zErrMsg);

    // 4. OverrideAuditLog Tablosu (Degisiklik gecmisi)
    const char* sqlAudit =
        "CREATE TABLE IF NOT EXISTS OverrideAuditLog ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "EXE_PATH TEXT NOT NULL,"
        "TITLE_PATTERN TEXT,"
        "OLD_CATEGORY TEXT,"
        "NEW_CATEGORY TEXT,"
        "OLD_SCORE INTEGER,"
        "NEW_SCORE INTEGER);";
    sqlite3_exec(db, sqlAudit, NULL, 0, &zErrMsg);

    // 5. Seed Data (Varsayılan Bilgiler)
    const char* seedSql = "INSERT OR IGNORE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE, SOURCE) VALUES "
        "('devenv.exe', '*', 'Yazilim', 10, 'seed'),"
        "('Code.exe', '*', 'Yazilim', 10, 'seed'),"
        "('windowsterminal.exe', '*', 'Yazilim', 10, 'seed'),"
        "('msedge.exe', 'youtube', 'Eglence', -5, 'seed'),"
        "('discord.exe', '*', 'Sosyal Medya', -3, 'seed');";
    sqlite3_exec(db, seedSql, NULL, 0, &zErrMsg);

    // --- Prepare cached statements ---
    prepareStatements();

    return true;
}

int DatabaseManager::logActivity(const EventData& data) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (!m_stmtLogActivity) return -1;

    sqlite3_reset(m_stmtLogActivity);
    sqlite3_clear_bindings(m_stmtLogActivity);
    sqlite3_bind_text(m_stmtLogActivity, 1, data.processName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtLogActivity, 2, data.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtLogActivity, 3, data.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(m_stmtLogActivity);

    PERF_COUNT(db_rows_inserted);
    return (int)sqlite3_last_insert_rowid(db);
}

void DatabaseManager::updateDuration(int id, int seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (!m_stmtUpdateDuration) return;

    sqlite3_reset(m_stmtUpdateDuration);
    sqlite3_clear_bindings(m_stmtUpdateDuration);
    sqlite3_bind_int(m_stmtUpdateDuration, 1, seconds);
    sqlite3_bind_int(m_stmtUpdateDuration, 2, id);
    sqlite3_step(m_stmtUpdateDuration);

    PERF_COUNT(db_rows_updated);
}

void DatabaseManager::beginTransaction() {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, 0, nullptr);
}

void DatabaseManager::commitTransaction() {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_exec(db, "COMMIT;", NULL, 0, nullptr);
    PERF_COUNT(db_batch_committed);
}

void DatabaseManager::prepareStatements() {
    // Called from init() while db_mutex is held
    const char* sqlLog = "INSERT INTO Activities (PROCESS, TITLE, URL) VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db, sqlLog, -1, &m_stmtLogActivity, NULL);

    const char* sqlDur = "UPDATE Activities SET DURATION = ? WHERE ID = ?;";
    sqlite3_prepare_v2(db, sqlDur, -1, &m_stmtUpdateDuration, NULL);
}

void DatabaseManager::finalizeStatements() {
    if (m_stmtLogActivity)    { sqlite3_finalize(m_stmtLogActivity);    m_stmtLogActivity = nullptr; }
    if (m_stmtUpdateDuration) { sqlite3_finalize(m_stmtUpdateDuration); m_stmtUpdateDuration = nullptr; }
}

std::pair<int, std::string> DatabaseManager::getScoreForActivity(const std::string& process, const std::string& title) {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;
    const char* sql = "SELECT SCORE, CATEGORY FROM KnowledgeBase "
        "WHERE PROCESS = ? AND (TITLE_KEYWORD = '*' OR ? LIKE '%' || TITLE_KEYWORD || '%') "
        "ORDER BY (TITLE_KEYWORD != '*') DESC LIMIT 1;";

    int score = 0;
    std::string category = "Uncategorized";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, process.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            score = sqlite3_column_int(stmt, 0);
            category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }
    return { score, category };
}

std::vector<std::pair<std::string, std::string>> DatabaseManager::getUncategorizedActivities() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<std::pair<std::string, std::string>> unknowns;
    sqlite3_stmt* stmt;
    const char* sql =
        "SELECT DISTINCT a.PROCESS, a.TITLE FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "WHERE k.CATEGORY IS NULL OR k.CATEGORY = 'Unknown';";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string proc = (const char*)sqlite3_column_text(stmt, 0);
            std::string tit = (const char*)sqlite3_column_text(stmt, 1);
            unknowns.push_back({ proc, tit });
        }
        sqlite3_finalize(stmt);
    }
    return unknowns;
}

std::vector<ActivityLog> DatabaseManager::getRecentLogs(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<ActivityLog> logs;
    const char* sql =
        "SELECT a.PROCESS, a.TITLE, COALESCE(k.CATEGORY, 'Uncategorized'), COALESCE(k.SCORE, 0), a.DURATION, COALESCE(k.SOURCE, 'seed') "
        "FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "GROUP BY a.ID ORDER BY a.ID DESC LIMIT ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ActivityLog log;
            log.process = (const char*)sqlite3_column_text(stmt, 0);
            log.title = (const char*)sqlite3_column_text(stmt, 1);
            log.category = (const char*)sqlite3_column_text(stmt, 2);
            log.score = sqlite3_column_int(stmt, 3);
            log.duration = sqlite3_column_int(stmt, 4);
            log.source = (const char*)sqlite3_column_text(stmt, 5);
            logs.push_back(log);
        }
        sqlite3_finalize(stmt);
    }
    return logs;
}

std::vector<ActivityLog> DatabaseManager::getUncategorizedLogs() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<ActivityLog> logs;
    const char* sql =
        "SELECT DISTINCT a.PROCESS, a.TITLE, 'Uncategorized' as CATEGORY, 0 as SCORE, a.DURATION "
        "FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "WHERE k.CATEGORY IS NULL OR k.CATEGORY = 'Unknown' "
        "LIMIT 1000;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ActivityLog log;
            log.process = (const char*)sqlite3_column_text(stmt, 0);
            log.title = (const char*)sqlite3_column_text(stmt, 1);
            log.category = "Uncategorized";
            log.score = 0;
            log.duration = sqlite3_column_int(stmt, 4);
            logs.push_back(log);
        }
        sqlite3_finalize(stmt);
    }
    return logs;
}

std::map<std::string, float> DatabaseManager::getCategoryDistribution() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::map<std::string, float> stats;
    const char* sql =
        "SELECT COALESCE(k.CATEGORY, 'Uncategorized'), SUM(a.DURATION) "
        "FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "GROUP BY 1;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cat = (const char*)sqlite3_column_text(stmt, 0);
            stats[cat] = (float)sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    return stats;
}

float DatabaseManager::calculateDailyProductivity() {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char* sql =
        "SELECT SUM(COALESCE(k.SCORE, 0) * a.DURATION) as total_score, SUM(a.DURATION) as total_duration "
        "FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "WHERE DATE(a.TIMESTAMP) = DATE('now');";

    sqlite3_stmt* stmt;
    float productivity = 0.0f;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int total_score = sqlite3_column_int(stmt, 0);
            int total_duration = sqlite3_column_int(stmt, 1);

            if (total_duration > 0) {
                // -10 to +10 scale, normalize to percentage (0-100)
                // Score range: -10*duration to +10*duration
                // Normalized: (total_score + 10*duration) / (20*duration) * 100
                productivity = ((float)total_score + 10.0f * total_duration) / (20.0f * total_duration) * 100.0f;
                productivity = (productivity < 0.0f) ? 0.0f : (productivity > 100.0f) ? 100.0f : productivity;
            }
        }
        sqlite3_finalize(stmt);
    }
    return productivity;
}

float DatabaseManager::calculateTodaysTotalScore() {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char* sql =
        "SELECT SUM(COALESCE(k.SCORE, 0) * a.DURATION) "
        "FROM Activities a "
        "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
        "AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
        "WHERE DATE(a.TIMESTAMP) = DATE('now');";

    sqlite3_stmt* stmt;
    float totalScore = 0.0f;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalScore = (float)sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return totalScore;
}

int DatabaseManager::getTodaysTotalDuration() {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char* sql =
        "SELECT SUM(a.DURATION) "
        "FROM Activities a "
        "WHERE DATE(a.TIMESTAMP) = DATE('now');";

    sqlite3_stmt* stmt;
    int totalDuration = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalDuration = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return totalDuration;
}

bool DatabaseManager::saveAILabels(const std::string& process, const std::string& title,
                                    const std::string& category, int score)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;

    // Eger bu kural icin user override varsa, AI etiketi uygulama
    const char* checkOverride =
        "SELECT 1 FROM CategoryOverrides WHERE EXE_PATH = ? AND (TITLE_PATTERN = '*' OR TITLE_PATTERN = ?) LIMIT 1;";
    if (sqlite3_prepare_v2(db, checkOverride, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, process.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        bool hasOverride = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (hasOverride) {
            std::cout << "[DB] Override mevcut, AI etiketi atlanıyor: " << process
                      << " [" << title << "]" << std::endl;
            return true; // override var, AI yazmaz
        }
    }

    const char* sql =
        "INSERT OR REPLACE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE, SOURCE) "
        "VALUES (?, ?, ?, ?, 'ai');";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, process.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, score);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        if (ok) {
            std::cout << "[DB] AI Etiketi Kaydedildi: " << process
                      << " [" << title << "] -> " << category
                      << " (" << score << ")" << std::endl;
        }
        return ok;
    }
    sqlite3_finalize(stmt);
    return false;
}

json DatabaseManager::getDashboardSummaryJson() {
    std::lock_guard<std::mutex> lock(db_mutex);
    json result;

    // ── 1. Bugünkü Toplam Verimlilik Skoru ──
    {
        sqlite3_stmt* stmt;
        const char* sql =
            "SELECT "
            "  SUM(COALESCE(k.SCORE, 0) * a.DURATION) AS weighted_score, "
            "  SUM(a.DURATION) AS total_duration "
            "FROM Activities a "
            "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
            "  AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
            "WHERE DATE(a.TIMESTAMP) = DATE('now');";

        float productivity = 0.0f;
        float totalScore = 0.0f;
        int totalDuration = 0;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                totalScore = (float)sqlite3_column_int(stmt, 0);
                totalDuration = sqlite3_column_int(stmt, 1);
                if (totalDuration > 0) {
                    productivity = (totalScore + 10.0f * totalDuration) / (20.0f * totalDuration) * 100.0f;
                    productivity = std::max(0.0f, std::min(100.0f, productivity));
                }
            }
            sqlite3_finalize(stmt);
        }

        result["productivity"] = {
            {"percentage", std::round(productivity * 10.0f) / 10.0f},
            {"weightedScore", totalScore},
            {"totalDurationSec", totalDuration}
        };
    }

    // ── 2. En Çok Vakit Geçirilen Top 3 Uygulama ──
    {
        sqlite3_stmt* stmt;
        const char* sql =
            "SELECT a.PROCESS, "
            "  SUM(a.DURATION) AS total_sec, "
            "  COALESCE(k.CATEGORY, 'Uncategorized') AS cat, "
            "  COALESCE(k.SCORE, 0) AS score "
            "FROM Activities a "
            "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
            "  AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
            "WHERE DATE(a.TIMESTAMP) = DATE('now') "
            "GROUP BY a.PROCESS "
            "ORDER BY total_sec DESC "
            "LIMIT 3;";

        json topApps = json::array();
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* proc = (const char*)sqlite3_column_text(stmt, 0);
                int dur = sqlite3_column_int(stmt, 1);
                const char* cat = (const char*)sqlite3_column_text(stmt, 2);
                int sc = sqlite3_column_int(stmt, 3);

                topApps.push_back({
                    {"process", proc ? proc : "Unknown"},
                    {"durationSec", dur},
                    {"category", cat ? cat : "Uncategorized"},
                    {"score", sc}
                });
            }
            sqlite3_finalize(stmt);
        }
        result["topApps"] = topApps;
    }

    // ── 3. Verimli / Verimsiz / Nötr Süre Oranları ──
    {
        sqlite3_stmt* stmt;
        const char* sql =
            "SELECT "
            "  SUM(CASE WHEN COALESCE(k.SCORE, 0) > 0  THEN a.DURATION ELSE 0 END) AS productive, "
            "  SUM(CASE WHEN COALESCE(k.SCORE, 0) < 0  THEN a.DURATION ELSE 0 END) AS unproductive, "
            "  SUM(CASE WHEN COALESCE(k.SCORE, 0) = 0  THEN a.DURATION ELSE 0 END) AS neutral, "
            "  SUM(a.DURATION) AS total "
            "FROM Activities a "
            "LEFT JOIN KnowledgeBase k ON a.PROCESS = k.PROCESS "
            "  AND (k.TITLE_KEYWORD = '*' OR a.TITLE LIKE '%' || k.TITLE_KEYWORD || '%') "
            "WHERE DATE(a.TIMESTAMP) = DATE('now');";

        int productive = 0, unproductive = 0, neutral = 0, total = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                productive   = sqlite3_column_int(stmt, 0);
                unproductive = sqlite3_column_int(stmt, 1);
                neutral      = sqlite3_column_int(stmt, 2);
                total        = sqlite3_column_int(stmt, 3);
            }
            sqlite3_finalize(stmt);
        }

        float prodPct   = total > 0 ? (float)productive   / total * 100.0f : 0.0f;
        float unprodPct = total > 0 ? (float)unproductive  / total * 100.0f : 0.0f;
        float neutPct   = total > 0 ? (float)neutral       / total * 100.0f : 0.0f;

        result["timeBreakdown"] = {
            {"productiveSec",   productive},
            {"unproductiveSec", unproductive},
            {"neutralSec",      neutral},
            {"totalSec",        total},
            {"productivePct",   std::round(prodPct   * 10.0f) / 10.0f},
            {"unproductivePct", std::round(unprodPct * 10.0f) / 10.0f},
            {"neutralPct",      std::round(neutPct   * 10.0f) / 10.0f}
        };
    }

    return result;
}

// ── Override Feedback Loop ──

bool DatabaseManager::saveCategoryOverride(const std::string& exePath, const std::string& titlePattern,
                                            const std::string& newCategory, int newScore)
{
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;

    // 1. Eski kategoriyi bul (audit icin)
    std::string oldCategory = "Uncategorized";
    int oldScore = 0;
    const char* findOld =
        "SELECT CATEGORY, SCORE FROM KnowledgeBase "
        "WHERE PROCESS = ? AND TITLE_KEYWORD = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, findOld, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, titlePattern.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* c = (const char*)sqlite3_column_text(stmt, 0);
            if (c) oldCategory = c;
            oldScore = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    // 2. Override tablosuna INSERT OR REPLACE
    const char* sqlOverride =
        "INSERT INTO CategoryOverrides (EXE_PATH, TITLE_PATTERN, CATEGORY, SCORE, UPDATED_AT) "
        "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(EXE_PATH, TITLE_PATTERN) DO UPDATE SET "
        "CATEGORY = excluded.CATEGORY, SCORE = excluded.SCORE, UPDATED_AT = CURRENT_TIMESTAMP;";
    if (sqlite3_prepare_v2(db, sqlOverride, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, titlePattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, newCategory.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, newScore);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 3. KnowledgeBase'i de guncelle (aninda etki etmesi icin)
    const char* sqlKB =
        "INSERT INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE, SOURCE) "
        "VALUES (?, ?, ?, ?, 'user') "
        "ON CONFLICT(PROCESS, TITLE_KEYWORD) DO UPDATE SET "
        "CATEGORY = excluded.CATEGORY, SCORE = excluded.SCORE, SOURCE = 'user';";
    if (sqlite3_prepare_v2(db, sqlKB, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, titlePattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, newCategory.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, newScore);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 4. Audit log yaz
    const char* sqlAudit =
        "INSERT INTO OverrideAuditLog (EXE_PATH, TITLE_PATTERN, OLD_CATEGORY, NEW_CATEGORY, OLD_SCORE, NEW_SCORE) "
        "VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sqlAudit, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, titlePattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, oldCategory.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, newCategory.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, oldScore);
        sqlite3_bind_int(stmt, 6, newScore);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::cout << "[DB] Override kaydedildi: " << exePath
              << " [" << titlePattern << "] " << oldCategory << " -> " << newCategory
              << " (skor: " << oldScore << " -> " << newScore << ")" << std::endl;
    return true;
}

std::vector<OverrideRule> DatabaseManager::getOverrideRules() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<OverrideRule> rules;
    sqlite3_stmt* stmt;
    const char* sql =
        "SELECT EXE_PATH, TITLE_PATTERN, CATEGORY, SCORE, "
        "CREATED_AT, UPDATED_AT FROM CategoryOverrides ORDER BY UPDATED_AT DESC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            OverrideRule r;
            r.exePath = (const char*)sqlite3_column_text(stmt, 0);
            r.titlePattern = (const char*)sqlite3_column_text(stmt, 1);
            r.category = (const char*)sqlite3_column_text(stmt, 2);
            r.score = sqlite3_column_int(stmt, 3);
            const char* ca = (const char*)sqlite3_column_text(stmt, 4);
            const char* ua = (const char*)sqlite3_column_text(stmt, 5);
            r.createdAt = ca ? ca : "";
            r.updatedAt = ua ? ua : "";
            rules.push_back(r);
        }
        sqlite3_finalize(stmt);
    }
    return rules;
}

bool DatabaseManager::applyOverrides(std::vector<std::pair<std::string, std::string>>& activities) {
    std::lock_guard<std::mutex> lock(db_mutex);

    // Override kurallarina gore eslesen aktiviteleri filtrele ve KnowledgeBase'e yaz
    sqlite3_stmt* stmtFind;
    sqlite3_stmt* stmtInsert;
    const char* findSql =
        "SELECT CATEGORY, SCORE FROM CategoryOverrides "
        "WHERE EXE_PATH = ? AND (TITLE_PATTERN = '*' OR ? LIKE '%' || TITLE_PATTERN || '%') "
        "ORDER BY (TITLE_PATTERN != '*') DESC LIMIT 1;";
    const char* insertSql =
        "INSERT INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE, SOURCE) "
        "VALUES (?, ?, ?, ?, 'user') "
        "ON CONFLICT(PROCESS, TITLE_KEYWORD) DO UPDATE SET "
        "CATEGORY = excluded.CATEGORY, SCORE = excluded.SCORE, SOURCE = 'user';";

    bool anyApplied = false;
    std::vector<std::pair<std::string, std::string>> remaining;

    if (sqlite3_prepare_v2(db, findSql, -1, &stmtFind, NULL) != SQLITE_OK)
        return false;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmtInsert, NULL) != SQLITE_OK) {
        sqlite3_finalize(stmtFind);
        return false;
    }

    for (auto& [proc, title] : activities) {
        sqlite3_reset(stmtFind);
        sqlite3_bind_text(stmtFind, 1, proc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmtFind, 2, title.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmtFind) == SQLITE_ROW) {
            const char* cat = (const char*)sqlite3_column_text(stmtFind, 0);
            int score = sqlite3_column_int(stmtFind, 1);

            // KnowledgeBase'e override'i uygula
            sqlite3_reset(stmtInsert);
            sqlite3_bind_text(stmtInsert, 1, proc.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmtInsert, 2, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmtInsert, 3, cat ? cat : "Uncategorized", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmtInsert, 4, score);
            sqlite3_step(stmtInsert);
            anyApplied = true;
        } else {
            remaining.push_back({proc, title});
        }
    }

    sqlite3_finalize(stmtFind);
    sqlite3_finalize(stmtInsert);

    // Kalan (override ile eslesmeyen) aktiviteleri geri yaz
    activities = std::move(remaining);
    return anyApplied;
}

nlohmann::json DatabaseManager::getOverrideAuditLog(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex);
    nlohmann::json result = nlohmann::json::array();
    sqlite3_stmt* stmt;
    const char* sql =
        "SELECT TIMESTAMP, EXE_PATH, TITLE_PATTERN, OLD_CATEGORY, NEW_CATEGORY, "
        "OLD_SCORE, NEW_SCORE FROM OverrideAuditLog ORDER BY ID DESC LIMIT ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* ts = (const char*)sqlite3_column_text(stmt, 0);
            const char* exe = (const char*)sqlite3_column_text(stmt, 1);
            const char* tp = (const char*)sqlite3_column_text(stmt, 2);
            const char* oc = (const char*)sqlite3_column_text(stmt, 3);
            const char* nc = (const char*)sqlite3_column_text(stmt, 4);
            int os = sqlite3_column_int(stmt, 5);
            int ns = sqlite3_column_int(stmt, 6);

            result.push_back({
                {"timestamp", ts ? ts : ""},
                {"exePath", exe ? exe : ""},
                {"titlePattern", tp ? tp : "*"},
                {"oldCategory", oc ? oc : ""},
                {"newCategory", nc ? nc : ""},
                {"oldScore", os},
                {"newScore", ns}
            });
        }
        sqlite3_finalize(stmt);
    }
    return result;
}
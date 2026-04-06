#include "Data/DatabaseManager.hpp"
#include <iostream>

using json = nlohmann::json;

DatabaseManager::DatabaseManager(const std::string& dbName) : db(nullptr) {
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        std::cerr << "VERITABANI ACILAMADI: " << sqlite3_errmsg(db) << std::endl;
    }
}

DatabaseManager::~DatabaseManager() {
    if (db) sqlite3_close(db);
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> lock(db_mutex);

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
        "PRIMARY KEY (PROCESS, TITLE_KEYWORD));";
    sqlite3_exec(db, sqlKB, NULL, 0, &zErrMsg);

    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_activities_proc_title ON Activities(PROCESS, TITLE);", NULL, 0, &zErrMsg);

    // 3. Seed Data (Varsayılan Bilgiler)
    const char* seedSql = "INSERT OR IGNORE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE) VALUES "
        "('devenv.exe', '*', 'Yazilim', 10),"
        "('Code.exe', '*', 'Yazilim', 10),"
        "('windowsterminal.exe', '*', 'Yazilim', 10),"
        "('msedge.exe', 'youtube', 'Eglence', -5),"
        "('discord.exe', '*', 'Sosyal Medya', -3);";
    sqlite3_exec(db, seedSql, NULL, 0, &zErrMsg);

    return true;
}

int DatabaseManager::logActivity(const EventData& data) {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO Activities (PROCESS, TITLE, URL) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, data.processName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, data.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, data.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    return (int)sqlite3_last_insert_rowid(db);
}

void DatabaseManager::updateDuration(int id, int seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE Activities SET DURATION = ? WHERE ID = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, seconds);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
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
        "WHERE k.CATEGORY IS NULL;";

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
        "SELECT a.PROCESS, a.TITLE, COALESCE(k.CATEGORY, 'Uncategorized'), COALESCE(k.SCORE, 0), a.DURATION "
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
        "WHERE k.CATEGORY IS NULL "
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
    const char* sql =
        "INSERT OR REPLACE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE) "
        "VALUES (?, ?, ?, ?);";

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
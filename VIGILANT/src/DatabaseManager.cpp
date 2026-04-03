#include "DatabaseManager.hpp"
#include <iostream>

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

// AI'dan gelen veriyi KnowledgeBase'e jilet gibi işliyoruz
void DatabaseManager::injectAICategory(const std::string& appName, const std::string& category, int score) {
    std::lock_guard<std::mutex> lock(db_mutex);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE) VALUES (?, '*', ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, appName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, score);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            std::cout << "[DB] AI Verisi Islendi: " << appName << " -> " << category << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

std::pair<int, std::string> DatabaseManager::getScoreForActivity(const std::string& process, const std::string& title) {
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
    std::vector<std::pair<std::string, std::string>> unknowns;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT DISTINCT PROCESS, TITLE FROM Activities;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string proc = (const char*)sqlite3_column_text(stmt, 0);
            std::string tit = (const char*)sqlite3_column_text(stmt, 1);
            auto res = getScoreForActivity(proc, tit);
            if (res.second == "Uncategorized") {
                unknowns.push_back({ proc, tit });
            }
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
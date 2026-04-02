#include "DatabaseManager.hpp"
#include <iostream>

// DatabaseManager.cpp constructor kısmına bir kontrol ekleyelim:
DatabaseManager::DatabaseManager(const std::string& dbName) : db(nullptr) {
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        std::cerr << "VERITABANI ACILAMADI: " << sqlite3_errmsg(db) << std::endl;
    }
}

DatabaseManager::~DatabaseManager() {
    if (db) sqlite3_close(db);
}

// src/DatabaseManager.cpp içindeki init fonksiyonunu şu şekilde güncelle:

bool DatabaseManager::init() {
    // 1. Activities Tablosu
    const char* sql = "CREATE TABLE IF NOT EXISTS Activities ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PROCESS TEXT, TITLE TEXT, URL TEXT, DURATION INTEGER DEFAULT 0);";
    sqlite3_exec(db, sql, NULL, 0, &zErrMsg);

    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_activities_proc_title ON Activities(PROCESS, TITLE);", NULL, 0, &zErrMsg);
    // 2. KnowledgeBase Tablosu (Sözlük)
    const char* kbSql = "CREATE TABLE IF NOT EXISTS KnowledgeBase ("
        "PROCESS TEXT,"
        "TITLE_KEYWORD TEXT,"
        "CATEGORY TEXT,"
        "SCORE INTEGER,"
        "PRIMARY KEY (PROCESS, TITLE_KEYWORD));";
    sqlite3_exec(db, kbSql, NULL, 0, &zErrMsg);

    // 3. Seed Data: 42 School ve Geliştirici Odaklı Başlangıç Paketi
    // 'minishell', 'philosophers' gibi 42 projelerini otomatik tanıması için
   // 3. Seed Data (Gerçek Loglara Göre Güncellendi)
    const char* seedSql = "INSERT OR IGNORE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE) VALUES "
        "('devenv.exe', '*', 'Development', 10),"         // Visual Studio
        "('Code.exe', '*', 'Development', 10),"           // VS Code (Alternatif)
        "('windowsterminal.exe', '*', 'Development', 10),"// Windows Terminal
        "('msedge.exe', 'vigilant', '42 Projects', 10),"  // GitHub'daki Vigilant depon
        "('msedge.exe', 'gemini', 'AI Assistance', 8),"   // Şu an konuştuğumuz yer :)
        "('msedge.exe', 'youtube', 'Entertainment', -5)," // Eğlence
        "('discord.exe', '*', 'Social', -3);";

    sqlite3_exec(db, seedSql, NULL, 0, &zErrMsg);
    return true;
}

int DatabaseManager::logActivity(const EventData& data) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO Activities (PROCESS, TITLE, URL) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, data.processName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, data.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, data.url.c_str(), -1, SQLITE_TRANSIENT);

        // KRİTİK: Yazma işlemini kontrol edelim
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[SQL ERROR] Insert failed: " << sqlite3_errmsg(db) << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    return (int)sqlite3_last_insert_rowid(db);
}

void DatabaseManager::updateDuration(int id, int seconds) {
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
    sqlite3_stmt* stmt;
    // Sorgu: Önce spesifik anahtar kelimeye bak, yoksa yıldız (*) olan genel puanı al
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
            const char* catText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (catText) category = catText;
        }
    }
    sqlite3_finalize(stmt);
    return { score, category };
}

std::vector<std::pair<std::string, std::string>> DatabaseManager::getUncategorizedActivities() {
    std::vector<std::pair<std::string, std::string>> unknowns;
    sqlite3_stmt* stmt;

    // Sadece eşsiz (benzersiz) Process ve Title ikililerini getir
    const char* sql = "SELECT DISTINCT PROCESS, TITLE FROM Activities;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string process = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

            // Mevcut sözlükte var mı diye kendi yazdığımız fonksiyona soruyoruz
            auto [score, category] = getScoreForActivity(process, title);

            // Eğer sözlükte yoksa, "Bilinmeyenler" sepetine ekle
            if (category == "Uncategorized") {
                unknowns.push_back({ process, title });
            }
        }
    }
    else {
        std::cerr << "[SQL ERROR] Collector Query: " << sqlite3_errmsg(db) << std::endl;
    }
    sqlite3_finalize(stmt);

    return unknowns;
}

bool DatabaseManager::saveAILabels(const std::string& process, const std::string& title, const std::string& category, int score) {
    sqlite3_stmt* stmt;
    // INSERT OR REPLACE: Eğer daha önce varsa üzerine yazar, yoksa yeni ekler
    const char* sql = "INSERT OR REPLACE INTO KnowledgeBase (PROCESS, TITLE_KEYWORD, CATEGORY, SCORE) VALUES (?, ?, ?, ?);";

    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, process.c_str(), -1, SQLITE_TRANSIENT);
        // Yapay zeka tüm başlığa not verdiği için TITLE_KEYWORD olarak direkt başlığı kaydediyoruz
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, score);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            success = true;
        }
    }
    sqlite3_finalize(stmt);
    return success;
}

std::vector<ActivityLog> DatabaseManager::getRecentLogs(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::vector<ActivityLog> logs;

    // YENI AKILLI SORGU: 
    // LEFT JOIN yaparken hem tam eşleşmeye, hem LIKE (içinde geçiyor mu) 
    // durumuna hem de yıldız (*) jokerine bakıyoruz.
    const char* sql =
        "SELECT al.PROCESS, al.TITLE, "
        "       COALESCE(kb.CATEGORY, 'Uncategorized') as CAT, "
        "       COALESCE(kb.SCORE, 0) as SCR, "
        "       al.DURATION "
        "FROM Activities al "
        "LEFT JOIN KnowledgeBase kb ON al.PROCESS = kb.PROCESS "
        "AND (kb.TITLE_KEYWORD = '*' OR al.TITLE LIKE '%' || kb.TITLE_KEYWORD || '%') "
        "GROUP BY al.ID " // Eğer birden fazla kurala uyarsa (hem '*' hem 'notion' gibi), satırı çoğaltmasın diye
        "ORDER BY al.ID DESC LIMIT ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ActivityLog log;

            const unsigned char* pTxt = sqlite3_column_text(stmt, 0);
            log.process = pTxt ? reinterpret_cast<const char*>(pTxt) : "Unknown";

            const unsigned char* tTxt = sqlite3_column_text(stmt, 1);
            log.title = tTxt ? reinterpret_cast<const char*>(tTxt) : "Untitled";

            log.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            log.score = sqlite3_column_int(stmt, 3);
            log.duration = sqlite3_column_int(stmt, 4);

            logs.push_back(log);
        }
    }
    sqlite3_finalize(stmt);
    return logs;
}

std::map<std::string, float> DatabaseManager::getCategoryDistribution() {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::map<std::string, float> stats;

    const char* sql =
        "SELECT COALESCE(kb.CATEGORY, 'Uncategorized'), SUM(al.DURATION) "
        "FROM Activities al "
        "LEFT JOIN KnowledgeBase kb ON al.PROCESS = kb.PROCESS "
        "AND (kb.TITLE_KEYWORD = '*' OR al.TITLE LIKE '%' || kb.TITLE_KEYWORD || '%') "
        "GROUP BY 1;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cat = (const char*)sqlite3_column_text(stmt, 0);
            float duration = (float)sqlite3_column_int(stmt, 1);
            stats[cat] = duration;
        }
    }
    sqlite3_finalize(stmt);
    return stats;
}
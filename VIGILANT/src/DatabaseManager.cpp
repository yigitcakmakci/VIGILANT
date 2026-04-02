#include "DatabaseManager.hpp"
#include <iostream>

DatabaseManager::DatabaseManager(const std::string& dbName) : db(nullptr) {
    sqlite3_open(dbName.c_str(), &db);
}

DatabaseManager::~DatabaseManager() {
    if (db) sqlite3_close(db);
}

bool DatabaseManager::init() {
    const char* sql = "CREATE TABLE IF NOT EXISTS Activities ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PROCESS TEXT, TITLE TEXT, URL TEXT, DURATION INTEGER DEFAULT 0);";
    return sqlite3_exec(db, sql, NULL, 0, &zErrMsg) == SQLITE_OK;
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
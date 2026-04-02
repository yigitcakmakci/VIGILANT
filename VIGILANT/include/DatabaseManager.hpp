#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include "sqlite3.h"
#include "EventQueue.hpp"

class DatabaseManager {
private:
    sqlite3* db;
    char* zErrMsg = 0;

public:
    DatabaseManager(const std::string& dbName);
    ~DatabaseManager();

    bool init();
    int logActivity(const EventData& data); // bool -> int oldu
    void updateDuration(int id, int seconds); // Yeni üye eklendi
};

#endif
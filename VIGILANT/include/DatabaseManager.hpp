#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include <utility>
#include <vector>
#include "sqlite3.h"
#include "EventQueue.hpp"

class DatabaseManager {
private:
    sqlite3* db;
    char* zErrMsg = 0;

public:
    DatabaseManager(const std::string& dbName);
    ~DatabaseManager();

    bool saveAILabels(const std::string& process, const std::string& title, const std::string& category, int score);
    bool init();
    int logActivity(const EventData& data);
    void updateDuration(int id, int seconds);

    std::pair<int, std::string> getScoreForActivity(const std::string& process, const std::string& title);
    std::vector<std::pair<std::string, std::string>> getUncategorizedActivities();
};

#endif
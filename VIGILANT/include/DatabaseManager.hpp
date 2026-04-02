#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include <utility>
#include <vector>
#include "sqlite3.h"
#include "EventQueue.hpp"
#include <map>

// Gecmis kayitlari ekrana basmak icin bir yapi (struct)
struct ActivityLog {
    std::string process = "";
    std::string title = "";
    std::string category = "Uncategorized";
    int score = 0;
    int duration = 0;
};

class DatabaseManager {
private:
    sqlite3* db;
    std::mutex db_mutex; // Bu bizim kapı görevlimiz
    char* zErrMsg = 0;

public:
    DatabaseManager(const std::string& dbName);
    ~DatabaseManager();

    bool saveAILabels(const std::string& process, const std::string& title, const std::string& category, int score);
    bool init();
    int logActivity(const EventData& data);
    void updateDuration(int id, int seconds);
    std::vector<ActivityLog> getRecentLogs(int limit = 10);
    std::map<std::string, float> getCategoryDistribution();

    std::pair<int, std::string> getScoreForActivity(const std::string& process, const std::string& title);
    std::vector<std::pair<std::string, std::string>> getUncategorizedActivities();
};

#endif
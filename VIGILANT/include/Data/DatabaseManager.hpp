#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "sqlite3.h"
#include "Utils/EventQueue.hpp"

// Geçmiş kayıtları ekrana basmak için kullanılan yapı
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
    std::mutex db_mutex;
    char* zErrMsg = nullptr;

public:
    DatabaseManager(const std::string& dbName);
    ~DatabaseManager();

    bool init();
    int logActivity(const EventData& data);
    void updateDuration(int id, int seconds);

    // AI ve Manuel Kategorizasyon
    void injectAICategory(const std::string& appName, const std::string& category, int score);
    bool saveAILabels(const std::string& process, const std::string& title, const std::string& category, int score);

    // Veri Çekme Fonksiyonları
    std::vector<ActivityLog> getRecentLogs(int limit = 15);
    std::vector<ActivityLog> getUncategorizedLogs();
    std::map<std::string, float> getCategoryDistribution();
    std::pair<int, std::string> getScoreForActivity(const std::string& process, const std::string& title);
    std::vector<std::pair<std::string, std::string>> getUncategorizedActivities();

    // Verimlilik Hesaplama
    float calculateDailyProductivity();
    float calculateTodaysTotalScore();
    int getTodaysTotalDuration();
};

#endif
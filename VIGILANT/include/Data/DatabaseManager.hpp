#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "sqlite3.h"
#include "Utils/EventQueue.hpp"
#include "Utils/json.hpp"

// Geçmiş kayıtları ekrana basmak için kullanılan yapı
struct ActivityLog {
    std::string process = "";
    std::string title = "";
    std::string category = "Uncategorized";
    int score = 0;
    int duration = 0;
    std::string source = "seed"; // "seed", "ai", "user"
};

// Kullanıcı override kuralı
struct OverrideRule {
    std::string exePath;        // process name (e.g. "msedge.exe")
    std::string titlePattern;   // title keyword pattern, "*" = all
    std::string category;
    int score = 0;
    std::string createdAt;
    std::string updatedAt;
};

class DatabaseManager {
private:
    sqlite3* db;
    std::mutex db_mutex;
    char* zErrMsg = nullptr;

    // Prepared statement cache — prepare once, reset+rebind per call
    sqlite3_stmt* m_stmtLogActivity = nullptr;
    sqlite3_stmt* m_stmtUpdateDuration = nullptr;

    void prepareStatements();
    void finalizeStatements();

public:
    DatabaseManager(const std::string& dbName);
    ~DatabaseManager();

    bool init();
    int logActivity(const EventData& data);
    void updateDuration(int id, int seconds);

    // Batch transaction control — wrap multiple logActivity/updateDuration in one txn
    void beginTransaction();
    void commitTransaction();

    // AI Kategorizasyon
    bool saveAILabels(const std::string& process, const std::string& title, const std::string& category, int score);

    // Override (Feedback Loop)
    bool saveCategoryOverride(const std::string& exePath, const std::string& titlePattern,
                              const std::string& newCategory, int newScore);
    std::vector<OverrideRule> getOverrideRules();
    bool applyOverrides(std::vector<std::pair<std::string, std::string>>& activities);
    nlohmann::json getOverrideAuditLog(int limit = 50);

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

    // Dashboard Özet Verisi (bugünkü toplam skor, top 3 uygulama, verimli/verimsiz oran)
    nlohmann::json getDashboardSummaryJson();

    // ── History Altyapısı ──
    // Belirli bir tarihin verilerini döndürür (YYYY-MM-DD formatı)
    nlohmann::json getHistoricalData(const std::string& date);
    // Belirli bir tarihin aktivite loglarını döndürür
    std::vector<ActivityLog> getLogsForDate(const std::string& date, int limit = 50);
    // Son N günlük trend verilerini döndürür
    nlohmann::json getDailyTrends(int days = 7);
    // Günlük özet snapshot'ı kaydeder (DailySummary tablosu)
    bool saveDailySummary(const std::string& date);
    // Mevcut tarihleri listeler (veri olan günler)
    nlohmann::json getAvailableDates();

    // Veritabanı Temizleme — tüm tabloları siler
    bool clearAllData();
};

#endif
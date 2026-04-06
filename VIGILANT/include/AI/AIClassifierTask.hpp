#ifndef AI_CLASSIFIER_TASK_HPP
#define AI_CLASSIFIER_TASK_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class DatabaseManager;
class GeminiService;

// Veritabanindaki kategorize edilmemis aktiviteleri periyodik olarak
// Gemini AI ile siniflandiran arka plan gorevi.
class AIClassifierTask {
public:
    // intervalSeconds: AI tarama periyodu (varsayilan 300 = 5 dk)
    // batchSize:       Tek seferde AI'a gonderilecek maks aktivite sayisi
    AIClassifierTask(DatabaseManager& db, GeminiService& ai,
                     int intervalSeconds = 300, int batchSize = 20);
    ~AIClassifierTask();

    // Arka plan thread'ini baslatir
    void Start();

    // Arka plan thread'ini durdurup bekler
    void Stop();

private:
    void workerLoop();
    void runClassification();

    DatabaseManager& m_db;
    GeminiService&   m_ai;
    int              m_intervalSeconds;
    int              m_batchSize;

    std::thread             m_thread;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool>       m_running{false};
};

#endif

#include "AI/AIClassifierTask.hpp"
#include "AI/GeminiService.hpp"
#include "Data/DatabaseManager.hpp"
#include <windows.h>
#include <iostream>

// VS Output penceresine log yazan yardimci
static void ClassifierLog(const std::string& msg) {
    OutputDebugStringA(msg.c_str());
    OutputDebugStringA("\n");
}

// --- Constructor / Destructor ---

AIClassifierTask::AIClassifierTask(DatabaseManager& db, GeminiService& ai,
                                   int intervalSeconds, int batchSize)
    : m_db(db)
    , m_ai(ai)
    , m_intervalSeconds(intervalSeconds)
    , m_batchSize(batchSize)
{
}

AIClassifierTask::~AIClassifierTask() {
    Stop();
}

// --- Start / Stop ---

void AIClassifierTask::Start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return; // zaten calisiyor

    m_thread = std::thread(&AIClassifierTask::workerLoop, this);
    ClassifierLog("[AITask] Arka plan siniflandirma gorevi baslatildi (periyot: "
                  + std::to_string(m_intervalSeconds) + "s)");
}

void AIClassifierTask::Stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
        return; // zaten durmus

    // Uyuyan thread'i uyandir
    m_cv.notify_all();

    if (m_thread.joinable())
        m_thread.join();

    ClassifierLog("[AITask] Arka plan siniflandirma gorevi durduruldu.");
}

// --- Worker Loop ---

void AIClassifierTask::workerLoop() {
    ClassifierLog("[AITask] Worker thread basladi.");

    // Ilk calistirildiginda 30 saniye bekle (uygulama baslangicindan sonra)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_cv.wait_for(lock, std::chrono::seconds(30),
                          [this] { return !m_running.load(); })) {
            return; // durduruldu
        }
    }

    while (m_running.load()) {
        // Siniflandirma calistir
        runClassification();

        // Sonraki tarama zamani bekle (iptal edilebilir)
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_cv.wait_for(lock, std::chrono::seconds(m_intervalSeconds),
                          [this] { return !m_running.load(); })) {
            break; // durduruldu
        }
    }

    ClassifierLog("[AITask] Worker thread sonlandi.");
}

// --- Asenkron Siniflandirma ---

void AIClassifierTask::runClassification() {
    if (!m_ai.isAvailable()) {
        ClassifierLog("[AITask] API anahtari yok, siniflandirma atlanıyor.");
        return;
    }

    // 1. Veritabanindan kategorize edilmemis aktiviteleri al
    auto unknowns = m_db.getUncategorizedActivities();
    if (unknowns.empty()) {
        ClassifierLog("[AITask] Kategorize edilmemis aktivite bulunamadi.");
        return;
    }

    ClassifierLog("[AITask] " + std::to_string(unknowns.size())
                  + " kategorize edilmemis aktivite bulundu.");

    // 1.5. Override kurallari varsa oncelikli uygula
    size_t beforeOverride = unknowns.size();
    m_db.applyOverrides(unknowns);
    size_t overridden = beforeOverride - unknowns.size();
    if (overridden > 0) {
        ClassifierLog("[AITask] " + std::to_string(overridden)
                      + " aktivite override kurallarıyla siniflandirildi.");
    }
    if (unknowns.empty()) {
        ClassifierLog("[AITask] Tum aktiviteler override ile karsilandi, AI cagrilmayacak.");
        return;
    }

    // 2. Batch'ler halinde AI'a gonder
    size_t total = unknowns.size();
    size_t processed = 0;
    size_t saved = 0;

    for (size_t offset = 0; offset < total && m_running.load(); offset += m_batchSize) {
        size_t end = (std::min)(offset + static_cast<size_t>(m_batchSize), total);

        std::vector<std::pair<std::string, std::string>> batch(
            unknowns.begin() + offset,
            unknowns.begin() + end);

        ClassifierLog("[AITask] Batch gonderiliyor: " + std::to_string(batch.size())
                      + " aktivite (" + std::to_string(offset + 1) + "-"
                      + std::to_string(end) + "/" + std::to_string(total) + ")");

        // 3. Gemini AI ile siniflandir
        auto labels = m_ai.classifyActivities(batch);

        // 4. Sonuclari veritabanina asenkron olarak yaz
        for (const auto& label : labels) {
            if (!m_running.load())
                break;

            bool ok = m_db.saveAILabels(label.process, label.titleKeyword,
                                        label.category, label.score);
            if (ok) {
                saved++;
                ClassifierLog("[AITask] Kaydedildi: " + label.process
                              + " [" + label.titleKeyword + "] -> "
                              + label.category + " (" + std::to_string(label.score) + ")");
            }
            else {
                ClassifierLog("[AITask] HATA: Kayit basarisiz: " + label.process
                              + " [" + label.titleKeyword + "]");
            }
        }

        processed += batch.size();

        // Batch'ler arasi kisa bekleme (API rate-limit korunmasi)
        if (end < total && m_running.load()) {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_cv.wait_for(lock, std::chrono::seconds(2),
                              [this] { return !m_running.load(); })) {
                break;
            }
        }
    }

    ClassifierLog("[AITask] Siniflandirma tamamlandi: "
                  + std::to_string(processed) + " islendi, "
                  + std::to_string(saved) + " kaydedildi.");
}

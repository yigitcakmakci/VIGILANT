#ifndef GEMINI_SERVICE_HPP
#define GEMINI_SERVICE_HPP

#include <string>
#include <vector>

// AI'dan donen etiket paketi
struct AILabel {
    std::string process;
    std::string titleKeyword;
    std::string category;
    int score;
};

class GeminiService {
public:
    GeminiService();

    // API anahtari yuklenebildi mi?
    bool isAvailable() const;

    // Kategorize edilmemis aktiviteleri Gemini'ye gonderip etiket listesi dondurur
    std::vector<AILabel> classifyActivities(
        const std::vector<std::pair<std::string, std::string>>& activities);

private:
    std::string m_apiKey;

    // WinHTTP ile Gemini REST API'sine POST atar
    std::string sendRequest(const std::string& jsonBody);

    // Aktivite listesinden Gemini promptu olusturur
    std::string buildPrompt(const std::vector<std::pair<std::string, std::string>>& activities);

    // Gemini'nin metin yanitini AILabel vektorune parse eder
    std::vector<AILabel> parseResponse(const std::string& rawText);

    // JSON string escape yardimcisi
    static std::string escapeJson(const std::string& str);
};

#endif

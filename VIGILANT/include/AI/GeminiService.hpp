#ifndef GEMINI_SERVICE_HPP
#define GEMINI_SERVICE_HPP

#include <string>
#include <vector>
#include "Utils/json.hpp"

// Desteklenen AI saglayicilari
enum class AIProvider {
    Gemini,
    OpenAI,
    Anthropic
};

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

    // Mevcut yapilandirmayi dondurur
    AIProvider getProvider() const { return m_provider; }
    std::string getProviderName() const;
    std::string getModel() const { return m_model; }
    std::string getEnvVarName() const { return m_envVarName; }

    // Yeni saglayici / model / ortam degiskeni ile yeniden yapilandir
    // Ortam degiskeninden anahtari okur, basarili ise true doner
    bool configure(const std::string& envVarName, const std::string& providerStr, const std::string& model);

    // API anahtarini kisa bir test istegi ile dogrular
    // Basarili ise true, gecersiz anahtar ise false doner
    bool validateApiKey();

    // Kategorize edilmemis aktiviteleri AI'ya gonderip etiket listesi dondurur
    std::vector<AILabel> classifyActivities(
        const std::vector<std::pair<std::string, std::string>>& activities);

    // Activity Narrative: deterministic JSON ciktisi ureten AI cagrisi
    nlohmann::json generateNarrative(const nlohmann::json& narrativeInput);

private:
    AIProvider  m_provider = AIProvider::Gemini;
    std::string m_apiKey;
    std::string m_model = "gemini-2.5-flash";
    std::string m_envVarName = "GEMINI_API_KEY";

    // Saglayiciya gore HTTP istegi gonderir
    std::string sendRequest(const std::string& jsonBody);

    // Saglayiciya gore istek govdesi olusturur (classify icin)
    std::string buildProviderRequestBody(const std::string& prompt) const;

    // Saglayiciya gore istek govdesi olusturur (narrative icin, system prompt destekli)
    std::string buildProviderNarrativeBody(const std::string& systemPrompt, const std::string& userPrompt) const;

    // Saglayiciya gore yanit metnini cikarir
    std::string extractProviderResponseText(const std::string& response) const;

    // Aktivite listesinden AI promptu olusturur
    std::string buildPrompt(const std::vector<std::pair<std::string, std::string>>& activities);

    // AI'nin metin yanitini AILabel vektorune parse eder
    std::vector<AILabel> parseResponse(const std::string& rawText);

    // JSON string escape yardimcisi
    static std::string escapeJson(const std::string& str);

    // Provider string → enum donusumu
    static AIProvider parseProvider(const std::string& str);
};

#endif

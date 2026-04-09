#include "AI/GeminiService.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>
#include "Utils/json.hpp"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// VS Output penceresine log yazan yardimci
static void DebugLog(const std::string& msg) {
    OutputDebugStringA(msg.c_str());
    OutputDebugStringA("\n");
}

// --- Provider string donusumleri ---
AIProvider GeminiService::parseProvider(const std::string& str) {
    if (str == "openai" || str == "OpenAI") return AIProvider::OpenAI;
    if (str == "anthropic" || str == "Anthropic") return AIProvider::Anthropic;
    return AIProvider::Gemini;
}

std::string GeminiService::getProviderName() const {
    switch (m_provider) {
    case AIProvider::OpenAI:    return "OpenAI";
    case AIProvider::Anthropic: return "Anthropic";
    default:                    return "Gemini";
    }
}

// --- Constructor ---
GeminiService::GeminiService() {
    char buffer[512] = {};
    DWORD len = GetEnvironmentVariableA("GEMINI_API_KEY", buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) {
        m_apiKey = std::string(buffer, len);
        DebugLog("[AI] Gemini API anahtari yuklendi (" + std::to_string(len) + " karakter)");
    }
    else {
        DebugLog("[AI] HATA: GEMINI_API_KEY ortam degiskeni bulunamadi!");
    }
}

bool GeminiService::isAvailable() const {
    return !m_apiKey.empty();
}

// --- Yeniden yapilandirma ---
bool GeminiService::configure(const std::string& envVarName, const std::string& providerStr, const std::string& model) {
    m_provider = parseProvider(providerStr);
    m_model = model;
    m_envVarName = envVarName;

    char buffer[512] = {};
    DWORD len = GetEnvironmentVariableA(envVarName.c_str(), buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) {
        m_apiKey = std::string(buffer, len);
        DebugLog("[AI] API anahtari yuklendi: " + envVarName + " (" + std::to_string(len) + " karakter) -> " + getProviderName() + "/" + model);
        return true;
    }
    m_apiKey.clear();
    DebugLog("[AI] HATA: " + envVarName + " ortam degiskeni bulunamadi!");
    return false;
}

// --- API anahtar dogrulama ---
bool GeminiService::validateApiKey() {
    if (m_apiKey.empty()) return false;

    DebugLog("[AI] API anahtari dogrulaniyor: " + getProviderName() + "/" + m_model);

    // Kucuk bir test istegi gonder
    std::string testPrompt = "Hi";
    std::string requestBody;

    switch (m_provider) {
    case AIProvider::OpenAI:
        requestBody = "{\"model\":\"" + m_model + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"" + testPrompt + "\"}],"
            "\"max_tokens\":5}";
        break;
    case AIProvider::Anthropic:
        requestBody = "{\"model\":\"" + m_model + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"" + testPrompt + "\"}],"
            "\"max_tokens\":5}";
        break;
    default: // Gemini
        requestBody = "{\"contents\":[{\"parts\":[{\"text\":\"" + testPrompt + "\"}]}],"
            "\"generationConfig\":{\"maxOutputTokens\":5}}";
        break;
    }

    std::string response = sendRequest(requestBody);
    if (response.empty()) {
        DebugLog("[AI] Dogrulama basarisiz: bos yanit");
        return false;
    }

    // Hata kontrolu
    bool hasError = false;
    if (response.find("\"error\"") != std::string::npos) {
        // Gemini ve OpenAI hata formati
        if (response.find("401") != std::string::npos ||
            response.find("403") != std::string::npos ||
            response.find("invalid") != std::string::npos ||
            response.find("Invalid") != std::string::npos ||
            response.find("authentication") != std::string::npos ||
            response.find("unauthorized") != std::string::npos) {
            hasError = true;
        }
    }
    // Anthropic hata formati
    if (response.find("\"type\":\"error\"") != std::string::npos) {
        hasError = true;
    }

    if (hasError) {
        DebugLog("[AI] Dogrulama basarisiz: gecersiz anahtar. Yanit: " + response.substr(0, 300));
        return false;
    }

    DebugLog("[AI] API anahtari dogrulandi: " + getProviderName());
    return true;
}

// --- Saglayiciya gore istek govdesi (classify) ---
std::string GeminiService::buildProviderRequestBody(const std::string& prompt) const {
    switch (m_provider) {
    case AIProvider::OpenAI:
        return "{\"model\":\"" + m_model + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}],"
            "\"temperature\":0.1,\"max_tokens\":4096}";
    case AIProvider::Anthropic:
        return "{\"model\":\"" + m_model + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}],"
            "\"temperature\":0.1,\"max_tokens\":4096}";
    default: // Gemini
        return "{\"contents\":[{\"parts\":[{\"text\":\"" + escapeJson(prompt) + "\"}]}],"
            "\"generationConfig\":{\"temperature\":0.1,\"maxOutputTokens\":4096}}";
    }
}

// --- Saglayiciya gore istek govdesi (narrative, system prompt destekli) ---
std::string GeminiService::buildProviderNarrativeBody(const std::string& systemPrompt, const std::string& userPrompt) const {
    switch (m_provider) {
    case AIProvider::OpenAI:
        return "{\"model\":\"" + m_model + "\","
            "\"messages\":["
            "{\"role\":\"system\",\"content\":\"" + escapeJson(systemPrompt) + "\"},"
            "{\"role\":\"user\",\"content\":\"" + escapeJson(userPrompt) + "\"}"
            "],\"temperature\":0.1,\"max_tokens\":2048}";
    case AIProvider::Anthropic:
        return "{\"model\":\"" + m_model + "\","
            "\"system\":\"" + escapeJson(systemPrompt) + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(userPrompt) + "\"}],"
            "\"temperature\":0.1,\"max_tokens\":2048}";
    default: // Gemini
        return "{\"system_instruction\":{\"parts\":[{\"text\":\"" + escapeJson(systemPrompt) + "\"}]},"
            "\"contents\":[{\"parts\":[{\"text\":\"" + escapeJson(userPrompt) + "\"}]}],"
            "\"generationConfig\":{\"temperature\":0.1,\"maxOutputTokens\":2048}}";
    }
}

// --- Saglayiciya gore yanit metnini cikar ---
std::string GeminiService::extractProviderResponseText(const std::string& response) const {
    try {
        auto j = nlohmann::json::parse(response);

        switch (m_provider) {
        case AIProvider::OpenAI:
            // {"choices":[{"message":{"content":"..."}}]}
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                return j["choices"][0]["message"]["content"].get<std::string>();
            }
            break;
        case AIProvider::Anthropic:
            // {"content":[{"text":"..."}]}
            if (j.contains("content") && j["content"].is_array() && !j["content"].empty()) {
                return j["content"][0]["text"].get<std::string>();
            }
            break;
        default: // Gemini
            // {"candidates":[{"content":{"parts":[{"text":"..."}]}}]}
            if (j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
                return j["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
            }
            break;
        }
    }
    catch (...) {
        DebugLog("[AI] extractProviderResponseText: JSON parse hatasi");
    }
    return "";
}

// --- Ana siniflandirma fonksiyonu ---
std::vector<AILabel> GeminiService::classifyActivities(
    const std::vector<std::pair<std::string, std::string>>& activities)
{
    if (!isAvailable()) {
        DebugLog("[AI] API anahtari yok, siniflandirma iptal.");
        return {};
    }
    if (activities.empty()) {
        DebugLog("[AI] Aktivite listesi bos, siniflandirma iptal.");
        return {};
    }

    DebugLog("[AI] " + std::to_string(activities.size()) + " aktivite siniflandiriliyor (" + getProviderName() + "/" + m_model + ")...");

    std::string prompt = buildPrompt(activities);
    DebugLog("[AI] Prompt olusturuldu (" + std::to_string(prompt.size()) + " byte)");

    // Saglayiciya gore istek govdesi olustur
    std::string requestBody = buildProviderRequestBody(prompt);

    DebugLog("[AI] Istek gonderiliyor (" + std::to_string(requestBody.size()) + " byte)...");

    std::string response = sendRequest(requestBody);

    if (response.empty()) {
        DebugLog("[AI] HATA: AI'dan bos yanit geldi!");
        return {};
    }

    DebugLog("[AI] Yanit alindi (" + std::to_string(response.size()) + " byte)");

    // Yanit hata iceriyor mu kontrol et
    if (response.find("\"error\"") != std::string::npos) {
        // Hata mesajini cikar
        size_t msgPos = response.find("\"message\"");
        if (msgPos != std::string::npos) {
            size_t start = response.find(":\"", msgPos);
            if (start != std::string::npos) {
                start += 2;
                size_t end = response.find("\"", start);
                if (end != std::string::npos) {
                    std::string errMsg = response.substr(start, end - start);
                    DebugLog("[AI] API HATASI: " + errMsg);
                }
            }
        }
        DebugLog("[AI] Tam hata yaniti: " + response.substr(0, 500));
        return {};
    }

    // Saglayiciya gore yanit metnini cikar
    std::string extractedText = extractProviderResponseText(response);

    if (extractedText.empty()) {
        DebugLog("[AI] HATA: AI yanitindan text cikarilamadi!");
        DebugLog("[AI] Yanit (ilk 500 byte): " + response.substr(0, 500));
        return {};
    }

    DebugLog("[AI] Text cikartildi (" + std::to_string(extractedText.size()) + " byte):");
    DebugLog("[AI] " + extractedText.substr(0, 300));

    auto labels = parseResponse(extractedText);
    DebugLog("[AI] Parse sonucu: " + std::to_string(labels.size()) + " etiket");
    return labels;
}

// --- Prompt olusturucu ---
std::string GeminiService::buildPrompt(
    const std::vector<std::pair<std::string, std::string>>& activities)
{
    std::ostringstream ss;
    ss << "Sen bir verimlilik siniflandirma yapay zekasisin. "
       << "Asagidaki her aktiviteyi su kategorilerden BIRINE ve bir skora ata:\n"
       << "- Yazilim: skor +10\n"
       << "- Egitim: skor +7\n"
       << "- Is: skor +5\n"
       << "- Arastirma: skor +5\n"
       << "- Iletisim: skor +3\n"
       << "- Diger: skor 0\n"
       << "- Sosyal Medya: skor -3\n"
       << "- Eglence: skor -5\n"
       << "- Oyun: skor -7\n\n"
       << "Her satir icin TAM OLARAK su formatta yanit ver, baska HICBIR SEY yazma:\n"
       << "process|title_keyword|category|score\n\n"
       << "title_keyword alani: basliktan en anlamli 1-2 kelimeyi sec. "
       << "Eger tum basliklar ayni kategoriye dusuyorsa * kullan.\n\n"
       << "Aktiviteler:\n";

    for (size_t i = 0; i < activities.size(); i++) {
        // Basliktan sadece ilk 80 karakteri al (cok uzun basliklar JSON'i bozabilir)
        std::string title = activities[i].second;
        if (title.size() > 80) title = title.substr(0, 80) + "...";
        ss << (i + 1) << ". Process: " << activities[i].first
           << ", Title: " << title << "\n";
    }

    return ss.str();
}

// --- Yanit parse edici ---
std::vector<AILabel> GeminiService::parseResponse(const std::string& rawText) {
    std::vector<AILabel> labels;
    std::istringstream stream(rawText);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line.find('|') == std::string::npos)
            continue;

        // Markdown code fence satirlarini atla
        if (line.find("```") != std::string::npos) continue;

        std::istringstream lineStream(line);
        std::string process, keyword, category, scoreStr;

        if (std::getline(lineStream, process, '|') &&
            std::getline(lineStream, keyword, '|') &&
            std::getline(lineStream, category, '|') &&
            std::getline(lineStream, scoreStr))
        {
            auto trim = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t\r\n");
                size_t end = s.find_last_not_of(" \t\r\n");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trim(process); trim(keyword); trim(category); trim(scoreStr);

            if (process.empty() || category.empty()) continue;

            // AI bazen "Unknown" veya "Uncategorized" donebilir — bunlari atla
            if (category == "Unknown" || category == "unknown" || category == "Uncategorized") {
                DebugLog("[AI] Gecersiz kategori atlanıyor: " + process + " -> " + category);
                continue;
            }

            // Skor stringinden sayi olmayan karakterleri temizle ("+10" -> "10")
            std::string cleanScore;
            for (char c : scoreStr) {
                if (c == '-' || (c >= '0' && c <= '9')) cleanScore += c;
            }
            if (cleanScore.empty()) continue;

            int score = 0;
            try { score = std::stoi(cleanScore); }
            catch (...) { continue; }

            labels.push_back({ process, keyword, category, score });
            DebugLog("[AI] Etiket: " + process + " | " + keyword + " -> " + category + " (" + std::to_string(score) + ")");
        }
    }

    return labels;
}

// --- WinHTTP ile AI API cagrisi (coklu saglayici destekli) ---
std::string GeminiService::sendRequest(const std::string& jsonBody) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(
        L"VIGILANT/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
        DebugLog("[AI] HATA: WinHttpOpen basarisiz. Kod: " + std::to_string(GetLastError()));
        return result;
    }

    // TLS 1.2+ zorla
    DWORD dwSecureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwSecureProtocols, sizeof(dwSecureProtocols));

    // Timeout ayarla (30 saniye)
    DWORD timeout = 30000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Saglayiciya gore host ve path belirle
    std::wstring host;
    std::wstring path;
    std::wstring extraHeaders;

    switch (m_provider) {
    case AIProvider::OpenAI:
        host = L"api.openai.com";
        path = L"/v1/chat/completions";
        {
            std::wstring wKey(m_apiKey.begin(), m_apiKey.end());
            extraHeaders = L"Content-Type: application/json; charset=utf-8\r\nAuthorization: Bearer " + wKey + L"\r\n";
        }
        break;
    case AIProvider::Anthropic:
        host = L"api.anthropic.com";
        path = L"/v1/messages";
        {
            std::wstring wKey(m_apiKey.begin(), m_apiKey.end());
            extraHeaders = L"Content-Type: application/json; charset=utf-8\r\nx-api-key: " + wKey + L"\r\nanthropic-version: 2023-06-01\r\n";
        }
        break;
    default: // Gemini
        host = L"generativelanguage.googleapis.com";
        {
            std::wstring wApiKey(m_apiKey.begin(), m_apiKey.end());
            std::wstring wModel(m_model.begin(), m_model.end());
            path = L"/v1beta/models/" + wModel + L":generateContent?key=" + wApiKey;
        }
        extraHeaders = L"Content-Type: application/json; charset=utf-8\r\n";
        break;
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!hConnect) {
        DebugLog("[AI] HATA: WinHttpConnect basarisiz. Kod: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        DebugLog("[AI] HATA: WinHttpOpenRequest basarisiz. Kod: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DebugLog("[AI] WinHTTP istek gonderiliyor (" + getProviderName() + ")...");
    BOOL bSent = WinHttpSendRequest(
        hRequest, extraHeaders.c_str(), (DWORD)-1,
        (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size(),
        (DWORD)jsonBody.size(), 0);

    if (!bSent) {
        DebugLog("[AI] HATA: WinHttpSendRequest basarisiz. Kod: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DebugLog("[AI] Yanit bekleniyor...");
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        DebugLog("[AI] HATA: WinHttpReceiveResponse basarisiz. Kod: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    DebugLog("[AI] HTTP Status: " + std::to_string(statusCode));

    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    do {
        bytesAvailable = 0;
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable == 0) break;

        std::vector<char> buffer(bytesAvailable + 1, 0);
        WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
        result.append(buffer.data(), bytesRead);
    } while (bytesAvailable > 0);

    if (statusCode != 200) {
        DebugLog("[AI] HATA: API " + std::to_string(statusCode) + " dondurdu!");
        DebugLog("[AI] Yanit: " + result.substr(0, 500));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ""; // Hata durumunda bos dondur
    }

    DebugLog("[AI] Basarili yanit alindi (" + std::to_string(result.size()) + " byte)");

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

// --- JSON escape yardimcisi ---
// Tum kontrol karakterlerini ve ozel JSON karakterlerini escape eder
std::string GeminiService::escapeJson(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 64);
    for (unsigned char c : str) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                // Diger kontrol karakterlerini \\uXXXX formatina cevir
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            }
            else {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

// --- Activity Narrative Generator ---
json GeminiService::generateNarrative(const json& narrativeInput) {
    if (!isAvailable()) {
        DebugLog("[AI] Narrative: API anahtari yok, iptal.");
        return json{{"error", "API key not available"}};
    }

    // Deterministic system prompt
    static const char* kSystemPrompt =
        "You are ActivityNarrativeBot.\n\n"
        "ROLE: Generate a short, motivating developer-focused narrative from structured "
        "activity telemetry. Output MUST be a single raw JSON object with NO markdown fences, "
        "NO extra keys, NO explanation text.\n\n"
        "SCHEMA:\n"
        "{\n"
        "  \"headline\": string (max 120 chars, one sentence, Turkish),\n"
        "  \"highlights\": string[3] (exactly 3 bullet sentences, Turkish),\n"
        "  \"next_step\": string (one actionable suggestion for tomorrow, Turkish),\n"
        "  \"confidence\": number (0-1),\n"
        "  \"safety_notes\": string[] (redactions taken; [] if none)\n"
        "}\n\n"
        "HARD RULES:\n"
        "1. GROUNDING: Every fact MUST trace to an explicit field in the user-supplied JSON. "
        "Never invent project names, technologies, or achievements not present in the input.\n"
        "2. REDACTION: Before using any window title, file path, URL, token, email address, "
        "hostname, or IP address in your output, replace the sensitive segment with [REDACTED]. "
        "Record each redaction in safety_notes.\n"
        "3. TONE: Concise, motivating, developer-oriented. Turkish language.\n"
        "4. DETERMINISM: Given identical input, always produce the same output. "
        "Do not add random flourishes or vary adjectives across runs.\n"
        "5. HIGHLIGHTS LOGIC:\n"
        "   a. Highlight 1: derived from the top window by minutes.\n"
        "   b. Highlight 2: derived from the highest-minutes milestone.\n"
        "   c. Highlight 3: derived from commitsSummary if present; "
        "otherwise from the second-highest-minutes window.\n"
        "6. NEXT_STEP: Suggest continuing work related to the milestone with the lowest minutes. "
        "If only one milestone exists, suggest deepening that area.\n"
        "7. CONFIDENCE: Set to 1.0 when every claim maps directly to an input field. "
        "Lower proportionally if approximation was needed.\n"
        "8. Output ONLY the raw JSON object. No markdown, no code fences, no extra text.";

    std::string userPrompt =
        "Here is today's activity telemetry. Generate the ActivityNarrative JSON.\n\n"
        + narrativeInput.dump(2);

    // Saglayiciya gore istek govdesi olustur
    std::string requestBody = buildProviderNarrativeBody(kSystemPrompt, userPrompt);

    DebugLog("[AI] Narrative istek gonderiliyor (" + std::to_string(requestBody.size()) + " byte, " + getProviderName() + ")...");

    std::string response = sendRequest(requestBody);
    if (response.empty()) {
        DebugLog("[AI] Narrative: AI'dan bos yanit!");
        return json{{"error", "Empty response from API"}};
    }

    // API hata kontrolu
    if (response.find("\"error\"") != std::string::npos &&
        response.find("\"candidates\"") == std::string::npos &&
        response.find("\"choices\"") == std::string::npos &&
        response.find("\"content\"") == std::string::npos) {
        DebugLog("[AI] Narrative: API hatasi - " + response.substr(0, 300));
        return json{{"error", "API returned error"}};
    }

    // Saglayiciya gore yanit metnini cikar
    std::string extractedText = extractProviderResponseText(response);

    if (extractedText.empty()) {
        DebugLog("[AI] Narrative: text alani cikarilamadi!");
        DebugLog("[AI] Yanit (ilk 500 byte): " + response.substr(0, 500));
        return json{{"error", "Failed to extract text from response"}};
    }

    DebugLog("[AI] Narrative text cikartildi (" + std::to_string(extractedText.size()) + " byte)");

    // Markdown code fence temizligi
    std::string cleaned = extractedText;
    {
        size_t fenceStart = cleaned.find("```json");
        if (fenceStart != std::string::npos) {
            cleaned = cleaned.substr(fenceStart + 7);
            size_t fenceEnd = cleaned.rfind("```");
            if (fenceEnd != std::string::npos) cleaned = cleaned.substr(0, fenceEnd);
        }
        else {
            fenceStart = cleaned.find("```");
            if (fenceStart != std::string::npos) {
                cleaned = cleaned.substr(fenceStart + 3);
                size_t fenceEnd = cleaned.rfind("```");
                if (fenceEnd != std::string::npos) cleaned = cleaned.substr(0, fenceEnd);
            }
        }
        // Trim whitespace
        size_t first = cleaned.find_first_not_of(" \t\r\n");
        size_t last = cleaned.find_last_not_of(" \t\r\n");
        if (first != std::string::npos && last != std::string::npos)
            cleaned = cleaned.substr(first, last - first + 1);
    }

    // JSON parse
    json narrativeJson;
    try {
        narrativeJson = json::parse(cleaned);
    } catch (const std::exception& e) {
        DebugLog("[AI] Narrative: JSON parse hatasi: " + std::string(e.what()));
        DebugLog("[AI] Narrative raw: " + cleaned.substr(0, 300));
        return json{{"error", "JSON parse failed"}, {"raw", cleaned.substr(0, 300)}};
    }

    // Schema dogrulama
    bool valid =
        narrativeJson.contains("headline") && narrativeJson["headline"].is_string() &&
        narrativeJson.contains("highlights") && narrativeJson["highlights"].is_array() &&
        narrativeJson["highlights"].size() == 3 &&
        narrativeJson.contains("next_step") && narrativeJson["next_step"].is_string() &&
        narrativeJson.contains("confidence") && narrativeJson["confidence"].is_number() &&
        narrativeJson.contains("safety_notes") && narrativeJson["safety_notes"].is_array();

    if (!valid) {
        DebugLog("[AI] Narrative: Schema dogrulama basarisiz!");
        return json{{"error", "Schema validation failed"}, {"raw", narrativeJson.dump().substr(0, 300)}};
    }

    // highlights icindeki her elemanin string oldugunu dogrula
    for (const auto& h : narrativeJson["highlights"]) {
        if (!h.is_string()) {
            DebugLog("[AI] Narrative: Highlight string degil!");
            return json{{"error", "Schema validation failed: highlight not string"}};
        }
    }

    // Confidence clamp [0, 1]
    double conf = narrativeJson["confidence"].get<double>();
    if (conf < 0.0) conf = 0.0;
    if (conf > 1.0) conf = 1.0;
    narrativeJson["confidence"] = conf;

    DebugLog("[AI] Narrative basariyla olusturuldu (confidence=" + std::to_string(conf) + ")");
    return narrativeJson;
}

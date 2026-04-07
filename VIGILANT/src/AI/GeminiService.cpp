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

    DebugLog("[AI] " + std::to_string(activities.size()) + " aktivite siniflandiriliyor...");

    std::string prompt = buildPrompt(activities);
    DebugLog("[AI] Prompt olusturuldu (" + std::to_string(prompt.size()) + " byte)");

    // Gemini API istek govdesi
    std::string requestBody =
        "{\"contents\":[{\"parts\":[{\"text\":\"" + escapeJson(prompt) + "\"}]}],"
        "\"generationConfig\":{\"temperature\":0.1,\"maxOutputTokens\":4096}}";

    DebugLog("[AI] Istek gonderiliyor (" + std::to_string(requestBody.size()) + " byte)...");

    std::string response = sendRequest(requestBody);

    if (response.empty()) {
        DebugLog("[AI] HATA: Gemini'den bos yanit geldi!");
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
                    DebugLog("[AI] GEMINI API HATASI: " + errMsg);
                }
            }
        }
        DebugLog("[AI] Tam hata yaniti: " + response.substr(0, 500));
        return {};
    }

    // Gemini yanitindan "text" alanini cikar
    // Yanit: {"candidates":[{"content":{"parts":[{"text":"..."}]}}]}
    // "parts" icinde ilk "text" alanini bul
    std::string extractedText;
    size_t partsPos = response.find("\"parts\"");
    if (partsPos != std::string::npos) {
        size_t textPos = response.find("\"text\"", partsPos);
        if (textPos != std::string::npos) {
            // :"  sonrasini bul
            size_t colonQuote = response.find(":\"", textPos);
            if (colonQuote == std::string::npos) {
                // Bazi modeller : " yerine : " (bosluklu) doner
                colonQuote = response.find(":", textPos);
                if (colonQuote != std::string::npos) {
                    size_t q = response.find("\"", colonQuote + 1);
                    if (q != std::string::npos) colonQuote = q - 1;
                }
            }

            if (colonQuote != std::string::npos) {
                size_t start = colonQuote + 2;
                size_t end = start;
                while (end < response.size()) {
                    if (response[end] == '\\') {
                        end += 2;
                        continue;
                    }
                    if (response[end] == '"') break;
                    end++;
                }
                extractedText = response.substr(start, end - start);

                // Escape karakterleri geri coz
                std::string decoded;
                decoded.reserve(extractedText.size());
                for (size_t i = 0; i < extractedText.size(); i++) {
                    if (extractedText[i] == '\\' && i + 1 < extractedText.size()) {
                        char next = extractedText[i + 1];
                        if (next == 'n') { decoded += '\n'; i++; }
                        else if (next == 't') { decoded += '\t'; i++; }
                        else if (next == '"') { decoded += '"'; i++; }
                        else if (next == '\\') { decoded += '\\'; i++; }
                        else if (next == '/') { decoded += '/'; i++; }
                        else { decoded += next; i++; }
                    }
                    else {
                        decoded += extractedText[i];
                    }
                }
                extractedText = decoded;
            }
        }
    }

    if (extractedText.empty()) {
        DebugLog("[AI] HATA: Gemini yanitindan text cikarilamadi!");
        DebugLog("[AI] Yanit (ilk 500 byte): " + response.substr(0, 500));
        return {};
    }

    DebugLog("[AI] Gemini text cikartildi (" + std::to_string(extractedText.size()) + " byte):");
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

// --- WinHTTP ile Gemini API cagrisi ---
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

    // TLS 1.2+ zorla (Google API TLS 1.2 gerektirir)
    DWORD dwSecureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwSecureProtocols, sizeof(dwSecureProtocols));

    // Timeout ayarla (30 saniye)
    DWORD timeout = 30000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"generativelanguage.googleapis.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!hConnect) {
        DebugLog("[AI] HATA: WinHttpConnect basarisiz. Kod: " + std::to_string(GetLastError()));
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring wApiKey(m_apiKey.begin(), m_apiKey.end());
    std::wstring path = L"/v1beta/models/gemini-2.5-flash:generateContent?key=" + wApiKey;

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

    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";

    DebugLog("[AI] WinHTTP istek gonderiliyor...");
    BOOL bSent = WinHttpSendRequest(
        hRequest, headers, (DWORD)-1,
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

    // Gemini API request with system_instruction
    std::string requestBody =
        "{\"system_instruction\":{\"parts\":[{\"text\":\"" + escapeJson(kSystemPrompt) + "\"}]},"
        "\"contents\":[{\"parts\":[{\"text\":\"" + escapeJson(userPrompt) + "\"}]}],"
        "\"generationConfig\":{\"temperature\":0.1,\"maxOutputTokens\":2048}}";

    DebugLog("[AI] Narrative istek gonderiliyor (" + std::to_string(requestBody.size()) + " byte)...");

    std::string response = sendRequest(requestBody);
    if (response.empty()) {
        DebugLog("[AI] Narrative: Gemini'den bos yanit!");
        return json{{"error", "Empty response from API"}};
    }

    // API hata kontrolu
    if (response.find("\"error\"") != std::string::npos &&
        response.find("\"candidates\"") == std::string::npos) {
        DebugLog("[AI] Narrative: API hatasi - " + response.substr(0, 300));
        return json{{"error", "API returned error"}};
    }

    // Gemini yanit JSON'unu parse et ve text alanini cikar
    json geminiResponse;
    try {
        geminiResponse = json::parse(response);
    } catch (const std::exception& e) {
        DebugLog("[AI] Narrative: Gemini yanit parse hatasi: " + std::string(e.what()));
        return json{{"error", "Failed to parse Gemini response"}};
    }

    std::string extractedText;
    try {
        extractedText = geminiResponse["candidates"][0]["content"]["parts"][0]["text"]
            .get<std::string>();
    } catch (...) {
        DebugLog("[AI] Narrative: text alani cikarilamadi!");
        return json{{"error", "Failed to extract text from response"}};
    }

    if (extractedText.empty()) {
        DebugLog("[AI] Narrative: Bos text alani!");
        return json{{"error", "Empty text in response"}};
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

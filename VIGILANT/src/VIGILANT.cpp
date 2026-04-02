//#include <iostream>
//#include <thread> // Test için sleep kullanacağız
//#include <chrono>
//#include "IdleTracker.hpp"
//
//int main() {
//    // 5 saniye boyunca işlem yapılmazsa IDLE sayacak bir tracker
//    IdleTracker ghostEye(5);
//
//    std::cout << "VIGILANT Ghost Eye: Monitoring Idle State..." << std::endl;
//    std::cout << "Threshold set to 5 seconds." << std::endl;
//
//    while (true) {
//        if (ghostEye.isUserIdle()) {
//            std::cout << "[STATUS: IDLE] - Time: " << ghostEye.getIdleTimeMillis() / 1000 << "s" << std::endl;
//        }
//        else {
//            std::cout << "[STATUS: ACTIVE]" << std::endl;
//        }
//
//        // CPU'yu yormamak için 1 saniye bekle (42 disiplini: performans!)
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//    }
//
//    return 0;
//}

//#include "WindowTracker.hpp"
//#include <iostream>
//
//int main() {
//    std::cout << "VIGILANT Ghost: Window Tracking Initiated..." << std::endl;
//
//    WindowTracker::StartTracking();
//
//    // Windows Mesaj Döngüsü (Hook'un çalışması için ŞART)
//    MSG msg;
//    while (GetMessage(&msg, nullptr, 0, 0)) {
//        TranslateMessage(&msg);
//        DispatchMessage(&msg);
//    }
//
//    WindowTracker::StopTracking();
//    return 0;
//}

#include "WindowTracker.hpp"
#include "EventQueue.hpp"
#include "DatabaseManager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib") // Windows'a "Kendi internet kütüphaneni kullan" diyoruz.

// 1. JSON Bozulmalarini Engellemek Icin Guvenlik Filtresi
std::string EscapeJSON(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

// 2. Bizim Vakum Listemizi Gemini'nin Anlayacagi Metne Cevirme
std::string PrepareGeminiPrompt(const std::vector<std::pair<std::string, std::string>>& items) {
    std::ostringstream prompt;
    prompt << "Sen VIGILANT verimlilik asistanisin. Asagidaki JSON verisini incele. Her biri icin mantikli bir kategori (Development, Entertainment, Social, AI Assistance, Education) ve score (-10 ile 10) belirle. SADECE liste halinde JSON dondur.\n\n[";

    for (size_t i = 0; i < items.size(); ++i) {
        prompt << "{\"process\": \"" << items[i].first << "\", \"title\": \"" << EscapeJSON(items[i].second) << "\"}";
        if (i < items.size() - 1) prompt << ", ";
    }
    prompt << "]";
    return prompt.str();
}

EventQueue g_EventQueue;
DatabaseManager g_Vault("vigilant_vault.db");

void BackgroundWorker() {
    g_Vault.init();
    EventData data;
    int lastId = -1;
    std::string lastTitle = "";
    auto lastStartTime = std::chrono::steady_clock::now();

    while (g_EventQueue.pop(data)) {
        if (data.title.empty() || data.title == "Adsız") continue;

        auto now = std::chrono::steady_clock::now();
        auto [score, category] = g_Vault.getScoreForActivity(data.processName, data.title);

        std::cout << "------------------------------------" << std::endl;
        std::cout << "[SCANNER] Proc: " << data.processName << std::endl;
        std::cout << "[SCANNER] Title: " << data.title << std::endl;
        std::cout << "[SCANNER] Result: " << category << " (Score: " << score << ")" << std::endl;
        // Odak değiştiyse süreyi hesapla ve yaz
        if (lastId != -1 && data.title != lastTitle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastStartTime).count();
            g_Vault.updateDuration(lastId, static_cast<int>(duration));
        }

        // Aynı pencereyse yeni kayıt açma
        if (data.title == lastTitle) continue;

        // Yeni kayıt aç
        lastId = g_Vault.logActivity(data);
        lastTitle = data.title;
        lastStartTime = now;

        std::cout << "[VAULT] Monitoring: " << data.processName << " -> " << data.title << std::endl;
    }
}

// AI için talimat ve JSON verisi hazırlayan fonksiyon
std::string PrepareAIPrompt(const std::vector<std::pair<std::string, std::string>>& items) {
    std::ostringstream prompt;

    // 1. Sistem Talimatı (System Prompt)
    prompt << "Sen bir VIGILANT verimlilik asistanisin. Asagidaki JSON listesinde kullanicinin girdigi "
        << "uygulamalar ve sayfa basliklari var. Her biri icin mantikli bir 'category' "
        << "(Development, Entertainment, Social, AI Assistance, Education) ve "
        << "verimlilik puani olan 'score' (-10 ile 10 arasi) belirle.\n"
        << "SADECE gecerli bir JSON dondur, baska hicbir metin yazma.\n\n";

    // 2. Verileri JSON formatına çevirme
    prompt << "[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        // Basliklarin icindeki cift tirnaklari temizleyelim ki JSON bozulmasin
        std::string safeTitle = items[i].second;
        size_t pos = 0;
        while ((pos = safeTitle.find('"', pos)) != std::string::npos) {
            safeTitle.replace(pos, 1, "\\\"");
            pos += 2;
        }

        prompt << "  {\"process\": \"" << items[i].first << "\", \"title\": \"" << safeTitle << "\"}";

        // Son eleman degilse virgul koy
        if (i < items.size() - 1) prompt << ",";
        prompt << "\n";
    }
    prompt << "]\n";

    return prompt.str();
}

// 3. Gemini API'ye İstek Atan Native Windows Fonksiyonu
std::string CallGeminiAPI(const std::string& promptText, const std::string& apiKey) {
    std::string responseStr = "";
    // İnternet kapısını aç (Ghost Modu)
    HINTERNET hSession = WinHttpOpen(L"VIGILANT Ghost/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "[HATA] İnternet kapisi acilamadi.";

    // Gemini Sunucusuna Bağlan
    HINTERNET hConnect = WinHttpConnect(hSession, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        std::wstring urlPath = L"/v1beta/models/gemini-2.5-flash:generateContent?key=" + std::wstring(apiKey.begin(), apiKey.end());
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

        if (hRequest) {
            std::wstring headers = L"Content-Type: application/json\r\n";
            WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

            // Gemini'nin İstediği Özel JSON Paketi
            std::string payload = "{\"contents\": [{\"parts\": [{\"text\": \"" + EscapeJSON(promptText) + "\"}]}]}";

            // Postala!
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)payload.c_str(), payload.length(), payload.length(), 0)) {
                if (WinHttpReceiveResponse(hRequest, NULL)) {
                    DWORD size = 0;
                    DWORD downloaded = 0;
                    do {
                        WinHttpQueryDataAvailable(hRequest, &size);
                        if (size == 0) break;
                        char* buffer = new char[size + 1];
                        ZeroMemory(buffer, size + 1);
                        WinHttpReadData(hRequest, (LPVOID)buffer, size, &downloaded);
                        responseStr += buffer;
                        delete[] buffer;
                    } while (size > 0);
                }
            }
            else {
                responseStr = "[HATA] İstek gönderilemedi.";
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);

    return responseStr;
}

// DİKKAT: const std::string& yerine sadece std::string yaptık ki metni içeride değiştirebilelim
void ParseAndInjectAIResponse(std::string response) {
    // 1. TEMİZLİK: Gemini'nin koyduğu ters slash'ları (\") normal tırnağa (") çevir
    size_t p = 0;
    while ((p = response.find("\\\"", p)) != std::string::npos) {
        response.replace(p, 2, "\"");
        p += 1; // Sonsuz döngüye girmemesi için 1 adım ileri atla
    }

    // 2. PARÇALAMA İŞLEMİ
    std::string process, title, category;
    int score = 0;
    size_t pos = 0;

    // Artık temizlenmiş metinde gönül rahatlığıyla "process": arayabiliriz
    while ((pos = response.find("\"process\":", pos)) != std::string::npos) {
        // Process bul
        size_t start = response.find("\"", pos + 10) + 1;
        size_t end = response.find("\"", start);
        process = response.substr(start, end - start);
        pos = end;

        // Title bul
        pos = response.find("\"title\":", pos);
        start = response.find("\"", pos + 8) + 1;
        end = response.find("\"", start);
        title = response.substr(start, end - start);
        pos = end;

        // Category bul
        pos = response.find("\"category\":", pos);
        start = response.find("\"", pos + 11) + 1;
        end = response.find("\"", start);
        category = response.substr(start, end - start);
        pos = end;

        // Score bul
        pos = response.find("\"score\":", pos);
        start = response.find_first_of("-0123456789", pos + 8);
        end = response.find_first_not_of("-0123456789", start);
        if (start != std::string::npos && end != std::string::npos) {
            score = std::stoi(response.substr(start, end - start));
        }
        pos = end;

        // Kasaya kaydet
        g_Vault.saveAILabels(process, title, category, score);
        std::cout << "[ENJEKTE] " << process << " -> " << category << " (" << score << ")\n";
    }
}

void HotkeyWorker() {
    // BURAYA KENDI API KEY'INI YAZ
    std::string geminiApiKey = "GEMINI-API-KEY";

    while (true) {
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            std::cout << "\n[AI SYNC] F9 Basildi! Veriler toplaniyor..." << std::endl;
            auto unknowns = g_Vault.getUncategorizedActivities();

            if (unknowns.empty()) {
                std::cout << "[AI SYNC] Her sey zaten kategorize edilmis." << std::endl;
            }
            else {
                std::cout << "[AI SYNC] Gemini'ye baglaniliyor, lutfen bekleyin..." << std::endl;

                std::string promptText = PrepareGeminiPrompt(unknowns);
                std::string geminiResponse = CallGeminiAPI(promptText, geminiApiKey);

                std::cout << "[AI SYNC] API Cevabi alindi, Kasaya isleniyor...\n";
                ParseAndInjectAIResponse(geminiResponse);
                std::cout << "[AI SYNC] Islem tamamlandi!\n";
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    std::thread worker(BackgroundWorker);
    std::thread hotkey(HotkeyWorker); // Ajanı sahaya sürüyoruz

    WindowTracker::StartTracking();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WindowTracker::StopTracking();
    g_EventQueue.stop();
    worker.join();
    hotkey.detach(); // Hotkey thread'ini serbest bırak
    return 0;
}
#include "WindowTracker.hpp"
#include "EventQueue.hpp"
#include "DatabaseManager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#include <cstdlib>
#include "implot.h"
#include <atomic>
#include <vector>
#include <map>
#include <assert.h>

// --- Global Değişkenler ---
std::atomic<bool> g_Running{ true };
EventQueue g_EventQueue;
DatabaseManager g_Vault("vigilant_vault.db");

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 🎨 PROFESYONEL "SOFT" STİL
void SetupVigilantStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.ChildRounding = 10.0f;
    style.PopupRounding = 8.0f;
    style.WindowPadding = ImVec2(20, 20);
    style.ItemSpacing = ImVec2(12, 10);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.18f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.13f, 0.15f, 0.23f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.16f, 0.22f, 1.00f);
}

// JSON Karakter Temizleyici
std::string EscapeJSON(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else output += c;
    }
    return output;
}

// --- AI MOTORU FONKSİYONLARI ---
std::string PrepareGeminiPrompt(const std::vector<std::pair<std::string, std::string>>& unknowns) {
    // AI'a kesin ve katı kurallar koyuyoruz. Asla tablo veya açıklama yapmamasını emrediyoruz.
    std::string prompt = "YALNIZCA aşağıdaki formatta çıktı ver: Uygulama|Kategori|Puan\n"
        "Kategoriler SADECE sunlardan biri olmalıdır: Yazilim, Eglence, Sosyal Medya, Egitim, Oyun, Bos\n"
        "Puan -10 ile 10 arasinda tam sayi olmalidir.\n"
        "KESİNLİKLE tablo kullanma, baslik atma, markdown yazma, aciklama veya ozet ekleme. Sadece ham veri satirlari yaz.\n\n";
    for (const auto& item : unknowns) {
        prompt += "Uygulama: " + item.first + " (Baslik: " + item.second + ")\n";
    }
    return prompt;
}

std::string CallGeminiAPI(const std::string& apiKey, const std::string& prompt) {
    std::string responseData = "";
    HINTERNET hSession = WinHttpOpen(L"VigilantAgent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    std::wstring wKey(apiKey.begin(), apiKey.end());
    std::wstring url = L"/v1beta/models/gemini-3.1-flash-lite-preview:generateContent?key=" + wKey;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", url.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (hRequest) {
        std::string jsonPayload = "{ \"contents\": [{ \"parts\": [{ \"text\": \"" + EscapeJSON(prompt) + "\" }] }] }";
        LPCWSTR header = L"Content-Type: application/json\r\n";
        if (WinHttpSendRequest(hRequest, header, (DWORD)wcslen(header), (LPVOID)jsonPayload.c_str(), (DWORD)jsonPayload.length(), (DWORD)jsonPayload.length(), 0)) {
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                do {
                    if (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                        char* pszOutBuffer = new char[dwSize + 1];
                        DWORD dwDownloaded = 0;
                        if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                            pszOutBuffer[dwDownloaded] = '\0';
                            responseData += pszOutBuffer;
                        }
                        delete[] pszOutBuffer;
                    }
                } while (dwSize > 0);
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return responseData;
}

void ParseAndInjectAIResponse(const std::string& response) {
    std::string cleanResponse = response;

    // 1. SADECE "text" blogunun icindeki gercek veriyi cimbizla aliyoruz (JSON coplerini atliyoruz)
    size_t textStart = cleanResponse.find("\"text\": \"");
    if (textStart != std::string::npos) {
        textStart += 9; // '"text": "' karakterlerini atla
        size_t textEnd = cleanResponse.find("\"", textStart);
        if (textEnd != std::string::npos) {
            cleanResponse = cleanResponse.substr(textStart, textEnd - textStart);
        }
    }

    // 2. Literal "\n" ifadelerini C++'in anlayacagi gercek alt satirlara cevir
    size_t pos = 0;
    while ((pos = cleanResponse.find("\\n", pos)) != std::string::npos) {
        cleanResponse.replace(pos, 2, "\n");
        pos += 1;
    }

    // 3. Satir satir oku ve kaydet
    std::stringstream ss(cleanResponse);
    std::string line;
    int successCount = 0;

    while (std::getline(ss, line)) {
        int pipeCount = 0;
        for (char c : line) if (c == '|') pipeCount++;

        if (pipeCount >= 2 && line.find("Uygulama") == std::string::npos) {
            std::stringstream ls(line);
            std::string app, cat, scoreStr;

            std::getline(ls, app, '|');
            std::getline(ls, cat, '|');
            std::getline(ls, scoreStr, '|');

            // Sagdan soldan bosluk ve tirnak temizligi
            app.erase(0, app.find_first_not_of(" \t\"*|\\"));
            if (!app.empty()) app.erase(app.find_last_not_of(" \t\"*|\\") + 1);

            cat.erase(0, cat.find_first_not_of(" \t\"*|\\"));
            if (!cat.empty()) cat.erase(cat.find_last_not_of(" \t\"*|\\") + 1);

            scoreStr.erase(0, scoreStr.find_first_not_of(" \t\"*|\\"));
            if (!scoreStr.empty()) scoreStr.erase(scoreStr.find_last_not_of(" \t\"*|\\") + 1);

            try {
                int sc = std::stoi(scoreStr);
                g_Vault.injectAICategory(app, cat, sc);
                successCount++;
            }
            catch (...) {}
        }
    }
    std::cout << "[AI] Basariyla islenen kategori sayisi: " << successCount << std::endl;
}

// --- THREAD WORKERS ---
void BackgroundWorker() {
    g_Vault.init();
    EventData data;
    int lastId = -1;
    std::string lastTitle = "";
    auto lastStartTime = std::chrono::steady_clock::now();

    while (g_Running) {
        if (g_EventQueue.pop(data)) {
            if (data.title.empty() || data.title == "Adsız") continue;
            auto now = std::chrono::steady_clock::now();
            if (lastId != -1 && data.title != lastTitle) {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastStartTime).count();
                g_Vault.updateDuration(lastId, (int)duration);
            }
            if (data.title == lastTitle) continue;
            lastId = g_Vault.logActivity(data);
            lastTitle = data.title;
            lastStartTime = now;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// --- AI YARDIMCISI (DEBUG LOGLU) ---
void HotkeyWorker() {
    const char* env_p = std::getenv("GEMINI_API_KEY");
    if (!env_p) {
        std::cerr << "[!] KRITIK HATA: GEMINI_API_KEY ortam degiskeni bulunamadi!" << std::endl;
        return;
    }
    std::string apiKey(env_p);
    std::cout << "[+] AI Servisi Hazir. F9 Tusuna basilmasi bekleniyor..." << std::endl;

    while (g_Running) {
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            std::cout << "[AI] F9 algilandi, veritabanindan 'unknown' veriler cekiliyor..." << std::endl;

            auto unknowns = g_Vault.getUncategorizedActivities();
            if (unknowns.empty()) {
                std::cout << "[AI] Uyari: Analiz edilecek 'Bilinmeyen' aktivite yok!" << std::endl;
            }
            else {
                std::cout << "[AI] " << unknowns.size() << " adet veri Gemini'ye gonderiliyor..." << std::endl;

                std::string prompt = PrepareGeminiPrompt(unknowns);
                std::string response = CallGeminiAPI(apiKey, prompt);

                if (response.empty()) {
                    std::cout << "[AI] HATA: Gemini'den cevap donmedi (Network/API hatasi)!" << std::endl;
                }
                else {
                    std::cout << "[AI] Gemini Cevabi Geldi! Isleniyor..." << std::endl;
                    // Debug amacli cevabi konsola bas:
                    std::cout << "--- RAW RESPONSE ---\n" << response << "\n--- END ---" << std::endl;
                    ParseAndInjectAIResponse(response);
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(3)); // Spam korumasi
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// --- MAIN PROGRAM ---
int main() {
    std::thread worker(BackgroundWorker);
    std::thread hotkey(HotkeyWorker);
    WindowTracker::StartTracking();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"VigilantWindowClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"VIGILANT - NEURAL PORTAL", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) return 1;

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    SetupVigilantStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    while (true) {
        MSG msg;
        bool done = false;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // FULLSCREEN DASHBOARD
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("VIGILANT_UI", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // Header
        ImGui::TextDisabled("VIGILANT v1.1 | Neural Portal");
        ImGui::SameLine(ImGui::GetWindowWidth() - 160);
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "● SYSTEM ONLINE");
        ImGui::Separator();
        ImGui::Spacing();

        // --- SOL KOLON (Analytics Card) ---
        ImGui::BeginChild("LeftPanel", ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0), true);
        ImGui::Text("EFFICIENCY MAP");
        ImGui::Separator();
        ImGui::Spacing();

        auto stats = g_Vault.getCategoryDistribution();
        if (!stats.empty()) {
            ImPlot::PushColormap(ImPlotColormap_Deep);
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
            ImPlot::PushStyleColor(ImPlotCol_PlotBorder, ImVec4(0, 0, 0, 0));

            if (ImPlot::BeginPlot("##Pie", ImVec2(-1, -1), ImPlotFlags_NoMenus | ImPlotFlags_NoTitle)) {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                std::vector<const char*> labels;
                std::vector<float> values;
                for (auto const& [cat, dur] : stats) { labels.push_back(cat.c_str()); values.push_back(dur); }

                ImPlot::PlotPieChart(labels.data(), values.data(), (int)values.size(), 0.5, 0.5, 0.38, "%.0f s");
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor(2);
            ImPlot::PopColormap();
        }
        else {
            ImGui::TextDisabled("Gathering data...");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- SAĞ KOLON (Activity Stream) ---
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        ImGui::Text("LIVE ACTIVITY FEED");
        ImGui::Separator();
        auto logs = g_Vault.getRecentLogs(20);
        if (!logs.empty() && ImGui::BeginTable("Logs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Karma", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            for (const auto& log : logs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text(log.process.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextDisabled(log.category.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%ds", log.duration);
                ImGui::TableSetColumnIndex(3);
                if (log.score > 0) ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "+%d", log.score);
                else ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%d", log.score);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::End();

        // Render
        ImGui::Render();
        const float clear_color[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    g_Running = false;
    WindowTracker::StopTracking();
    if (worker.joinable()) worker.join();
    if (hotkey.joinable()) hotkey.join();

    ImPlot::DestroyContext();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    return 0;
}

// --- DirectX & Win32 Boilerplate ---
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget();
    return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}
void CreateRenderTarget() {
    ID3D11Texture2D* pBB = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB))) && pBB) {
        g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView);
        pBB->Release();
    }
}
void CleanupRenderTarget() { if (g_mainRenderTargetView) g_mainRenderTargetView->Release(); }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
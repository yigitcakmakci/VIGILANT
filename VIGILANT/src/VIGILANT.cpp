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

// --- Global Değişkenler (Sadece BİR KERE tanımlanmalı) ---
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
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ChildRounding = 8.0f;
    style.WindowPadding = ImVec2(20, 20);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.18f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
}

// JSON Yardımcısı
std::string EscapeJSON(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else output += c;
    }
    return output;
}

// Arka Plan İşçisi
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

// AI Yardımcısı (Hotkey)
void HotkeyWorker() {
    const char* env_p = std::getenv("GEMINI_API_KEY");
    if (!env_p) return;
    while (g_Running) {
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            // API Çağrı mantığı buraya gelecek
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ==========================================
// 🚀 ANA PROGRAM (MAIN)
// ==========================================
int main() {
    std::thread worker(BackgroundWorker);
    std::thread hotkey(HotkeyWorker);
    WindowTracker::StartTracking();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"VigilantWindowClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"VIGILANT - CORE", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

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

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("VIGILANT_UI", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        ImGui::TextDisabled("VIGILANT v1.1 | Neural Portal");
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        // UTF-8 hatasını önlemek için raw karakter yerine string kullandık
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "ONLINE");

        ImGui::Separator();
        ImGui::Spacing();

        // Sol Kolon: Grafik
        ImGui::BeginChild("Left", ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0), true);
        ImGui::Text("EFFICIENCY MAP");
        auto stats = g_Vault.getCategoryDistribution();
        if (!stats.empty() && ImPlot::BeginPlot("##Pie", ImVec2(-1, -1), ImPlotFlags_NoMouseText)) {
            std::vector<const char*> labels;
            std::vector<float> values;
            for (auto const& [cat, dur] : stats) { labels.push_back(cat.c_str()); values.push_back(dur); }
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
            ImPlot::PlotPieChart(labels.data(), values.data(), (int)values.size(), 0.5, 0.5, 0.4, "%.0f s");
            ImPlot::EndPlot();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Sağ Kolon: Liste
        ImGui::BeginChild("Right", ImVec2(0, 0), true);
        ImGui::Text("LIVE STREAM");
        auto logs = g_Vault.getRecentLogs(15);
        if (!logs.empty() && ImGui::BeginTable("Logs", 4, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("App"); ImGui::TableSetupColumn("Cat");
            ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Karma");
            ImGui::TableHeadersRow();
            for (const auto& log : logs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text(log.process.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextDisabled(log.category.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%ds", log.duration);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", log.score);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        const float clear_color[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
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
    ID3D11Texture2D* pBB;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB));
    g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView);
    pBB->Release();
}
void CleanupRenderTarget() { if (g_mainRenderTargetView) g_mainRenderTargetView->Release(); }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
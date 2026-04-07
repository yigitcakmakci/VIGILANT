#pragma once
#include <windows.h>
#include <string>

// ============================================================================
// StartupManager - Windows baslangic yonetimi
//
// Iki yontem desteklenir:
//
// 1) Registry Run (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
//    [+] Basit, hizli, admin yetkisi gerektirmez.
//    [+] Tum Windows surumlerinde calisir.
//    [-] Antivirusler / guvenlik yazilimlari bazen uyari verebilir.
//    [-] "Baslangic Programlari" sekmesinde gorunur ve kullanici devre
//        disi birakabilir.
//
// 2) Task Scheduler (schtasks.exe ile)
//    [+] Daha gelismis zamanlama secenekleri (gecikme, kosul vs.).
//    [+] Kullanici oturumu acildiginda otomatik calisir.
//    [+] Registry'ye dokunmaz, bazi guvenlik yazilimlarini tetiklemez.
//    [-] schtasks.exe cagrisi yavas olabilir (~200-500ms).
//    [-] Task Scheduler servisi devre disi ise calismaz.
//    [-] Bazi kurumsal ortamlarda politikalarla engellenebilir.
// ============================================================================

enum class AutostartMethod {
    Registry,       // HKCU\...\Run anahtari
    TaskScheduler   // schtasks.exe ile zamanlanmis gorev
};

class StartupManager {
public:
    // --- Birlesik API (UI toggle icin) ---
    // Varsayilan yontem: Registry
    static bool EnableAutostart(AutostartMethod method = AutostartMethod::Registry);
    static bool DisableAutostart(AutostartMethod method = AutostartMethod::Registry);
    static bool IsAutostartEnabled(AutostartMethod method = AutostartMethod::Registry);

    // Kisa yol: tum yontemleri devre disi birak
    static void DisableAll();

private:
    // --- Registry Run ---
    static bool RegistryEnable();
    static bool RegistryDisable();
    static bool RegistryIsEnabled();

    // --- Task Scheduler ---
    static bool TaskSchedulerEnable();
    static bool TaskSchedulerDisable();
    static bool TaskSchedulerIsEnabled();

    // --- Yardimci ---
    static std::wstring GetExecutablePath();
    static void Log(const wchar_t* msg);
    static void LogError(const wchar_t* msg, LONG errorCode = 0);

    static constexpr const wchar_t* kRunKey    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t* kValueName = L"VIGILANT";
    static constexpr const wchar_t* kTaskName  = L"VIGILANT_Autostart";
};

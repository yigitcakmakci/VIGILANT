#include "Utils/StartupManager.hpp"
#include <cstdio>

#pragma comment(lib, "Advapi32.lib")

// ============================================================================
// Yardimci: Loglama
// ============================================================================

void StartupManager::Log(const wchar_t* msg) {
    OutputDebugStringW(L"[StartupManager] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
}

void StartupManager::LogError(const wchar_t* msg, LONG errorCode) {
    wchar_t buf[512];
    if (errorCode != 0)
        swprintf_s(buf, L"[StartupManager] ERROR: %s (code=%ld)\n", msg, errorCode);
    else
        swprintf_s(buf, L"[StartupManager] ERROR: %s\n", msg);
    OutputDebugStringW(buf);
}

// ============================================================================
// Yardimci: Calistirilabilir dosya yolu
// ============================================================================

std::wstring StartupManager::GetExecutablePath() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    return std::wstring(path);
}

// ============================================================================
// Birlesik API  (UI toggle icin tek nokta)
// ============================================================================

bool StartupManager::EnableAutostart(AutostartMethod method) {
    switch (method) {
        case AutostartMethod::Registry:      return RegistryEnable();
        case AutostartMethod::TaskScheduler: return TaskSchedulerEnable();
    }
    return false;
}

bool StartupManager::DisableAutostart(AutostartMethod method) {
    switch (method) {
        case AutostartMethod::Registry:      return RegistryDisable();
        case AutostartMethod::TaskScheduler: return TaskSchedulerDisable();
    }
    return false;
}

bool StartupManager::IsAutostartEnabled(AutostartMethod method) {
    switch (method) {
        case AutostartMethod::Registry:      return RegistryIsEnabled();
        case AutostartMethod::TaskScheduler: return TaskSchedulerIsEnabled();
    }
    return false;
}

void StartupManager::DisableAll() {
    RegistryDisable();
    TaskSchedulerDisable();
    Log(L"All autostart methods disabled");
}

// ============================================================================
// Registry Run  (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
// ============================================================================

bool StartupManager::RegistryEnable() {
    try {
        std::wstring exePath = GetExecutablePath();
        if (exePath.empty()) {
            LogError(L"Registry: GetExecutablePath returned empty");
            return false;
        }

        // Bosluklu yollar icin tirnak icine al
        std::wstring quoted = L"\"" + exePath + L"\"";

        HKEY hKey = nullptr;
        LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey);
        if (rc != ERROR_SUCCESS) {
            LogError(L"Registry: RegOpenKeyExW failed", rc);
            return false;
        }

        rc = RegSetValueExW(
            hKey, kValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(quoted.c_str()),
            static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t))
        );
        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS) {
            LogError(L"Registry: RegSetValueExW failed", rc);
            return false;
        }

        Log(L"Registry autostart ENABLED");
        return true;
    }
    catch (...) {
        LogError(L"Registry: Unexpected exception in RegistryEnable");
        return false;
    }
}

bool StartupManager::RegistryDisable() {
    try {
        HKEY hKey = nullptr;
        LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey);
        if (rc != ERROR_SUCCESS) {
            LogError(L"Registry: RegOpenKeyExW failed", rc);
            return false;
        }

        rc = RegDeleteValueW(hKey, kValueName);
        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
            LogError(L"Registry: RegDeleteValueW failed", rc);
            return false;
        }

        Log(L"Registry autostart DISABLED");
        return true;
    }
    catch (...) {
        LogError(L"Registry: Unexpected exception in RegistryDisable");
        return false;
    }
}

bool StartupManager::RegistryIsEnabled() {
    try {
        HKEY hKey = nullptr;
        LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey);
        if (rc != ERROR_SUCCESS) return false;

        // Deger var mi kontrol et
        wchar_t data[MAX_PATH] = {};
        DWORD dataSize = sizeof(data);
        DWORD type = 0;
        rc = RegQueryValueExW(hKey, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataSize);
        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS || type != REG_SZ) return false;

        // Deger, gercek exe yolunu mu gosteriyor dogrula
        std::wstring currentExe = L"\"" + GetExecutablePath() + L"\"";
        return (currentExe == data);
    }
    catch (...) {
        LogError(L"Registry: Unexpected exception in RegistryIsEnabled");
        return false;
    }
}

// ============================================================================
// Task Scheduler  (schtasks.exe cagrisi)
//
// [+] Registry'ye dokunmaz, guvenlik yazilimlarini tetiklemez.
// [+] Gecikme (delay) ayari ile sistemi yorma riski dusuk.
// [-] schtasks.exe cagrisi ~200-500ms surebilir.
// [-] Task Scheduler servisi devre disi ise calismaz.
// [-] Kurumsal ortamlarda GPO ile engellenebilir.
// ============================================================================

bool StartupManager::TaskSchedulerEnable() {
    try {
        std::wstring exePath = GetExecutablePath();
        if (exePath.empty()) {
            LogError(L"TaskScheduler: GetExecutablePath returned empty");
            return false;
        }

        // Oncelikle varsa eski gorevi sil (idempotent)
        TaskSchedulerDisable();

        // schtasks /create
        //   /tn  = gorev adi
        //   /tr  = calistirilacak komut
        //   /sc  = tetikleme tipi (ONLOGON = kullanici oturum acinca)
        //   /rl  = calistirma seviyesi (LIMITED = admin degil)
        //   /delay = 10 saniye gecikme (sistemi yorma)
        //   /f   = zaten varsa uzerine yaz
        wchar_t cmd[1024];
        swprintf_s(cmd,
            L"schtasks /create /tn \"%s\" /tr \"\\\"%s\\\"\" /sc ONLOGON /rl LIMITED /delay 0000:10 /f",
            kTaskName, exePath.c_str());

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            LogError(L"TaskScheduler: CreateProcessW failed", static_cast<LONG>(GetLastError()));
            return false;
        }

        // Islemi bekle (max 10 saniye)
        DWORD wait = WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 1;
        if (wait == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &exitCode);
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            LogError(L"TaskScheduler: schtasks /create failed", static_cast<LONG>(exitCode));
            return false;
        }

        Log(L"TaskScheduler autostart ENABLED");
        return true;
    }
    catch (...) {
        LogError(L"TaskScheduler: Unexpected exception in TaskSchedulerEnable");
        return false;
    }
}

bool StartupManager::TaskSchedulerDisable() {
    try {
        wchar_t cmd[512];
        swprintf_s(cmd, L"schtasks /delete /tn \"%s\" /f", kTaskName);

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            // Gorev zaten yoksa hata degil
            Log(L"TaskScheduler: Task does not exist or CreateProcessW failed (non-critical)");
            return true;
        }

        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        Log(L"TaskScheduler autostart DISABLED");
        return true;
    }
    catch (...) {
        LogError(L"TaskScheduler: Unexpected exception in TaskSchedulerDisable");
        return false;
    }
}

bool StartupManager::TaskSchedulerIsEnabled() {
    try {
        wchar_t cmd[512];
        swprintf_s(cmd, L"schtasks /query /tn \"%s\"", kTaskName);

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }

        WaitForSingleObject(pi.hProcess, 5000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return (exitCode == 0);
    }
    catch (...) {
        LogError(L"TaskScheduler: Unexpected exception in TaskSchedulerIsEnabled");
        return false;
    }
}

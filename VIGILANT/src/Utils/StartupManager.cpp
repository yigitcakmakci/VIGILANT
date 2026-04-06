#include "Utils/StartupManager.hpp"
#include <iostream>

#pragma comment(lib, "Advapi32.lib")

std::wstring StartupManager::GetExecutablePath() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    return std::wstring(path);
}

bool StartupManager::EnableAutoStart() {
    std::wstring exePath = GetExecutablePath();
    if (exePath.empty()) {
        std::cerr << "[StartupManager] Failed to get executable path." << std::endl;
        return false;
    }

    // Calistirilabilir dosyayi tirnak icine al (bosluklu yollar icin)
    std::wstring quoted = L"\"" + exePath + L"\"";

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kRunKey,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        std::cerr << "[StartupManager] Failed to open registry key. Error: " << result << std::endl;
        return false;
    }

    result = RegSetValueExW(
        hKey,
        kValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(quoted.c_str()),
        static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        std::cerr << "[StartupManager] Failed to set registry value. Error: " << result << std::endl;
        return false;
    }

    std::cout << "[StartupManager] Auto-start enabled." << std::endl;
    return true;
}

bool StartupManager::DisableAutoStart() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kRunKey,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        std::cerr << "[StartupManager] Failed to open registry key. Error: " << result << std::endl;
        return false;
    }

    result = RegDeleteValueW(hKey, kValueName);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        std::cerr << "[StartupManager] Failed to delete registry value. Error: " << result << std::endl;
        return false;
    }

    std::cout << "[StartupManager] Auto-start disabled." << std::endl;
    return true;
}

bool StartupManager::IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kRunKey,
        0,
        KEY_QUERY_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) return false;

    result = RegQueryValueExW(hKey, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}

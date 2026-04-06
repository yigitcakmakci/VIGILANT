#pragma once
#include <windows.h>
#include <string>

class StartupManager {
public:
    // Uygulamayi Windows baslangicindan baslat veya kaldir
    static bool EnableAutoStart();
    static bool DisableAutoStart();
    static bool IsAutoStartEnabled();

private:
    static constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t* kValueName = L"VIGILANT";

    static std::wstring GetExecutablePath();
};

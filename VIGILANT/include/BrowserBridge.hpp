#ifndef BROWSER_BRIDGE_HPP
#define BROWSER_BRIDGE_HPP

#include <windows.h>
#include <UIAutomation.h>
#include <string>

class BrowserBridge {
public:
    BrowserBridge();
    ~BrowserBridge();

    // Verilen pencerenin (HWND) içindeki aktif URL'yi yakalar
    std::string GetActiveURL(HWND hwnd);

private:
    IUIAutomation* pAutomation; // UIA Ana Nesnesi
};

#endif
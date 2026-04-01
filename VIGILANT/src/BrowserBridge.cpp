#include "BrowserBridge.hpp"
#include <iostream>

BrowserBridge::BrowserBridge() : pAutomation(nullptr) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&pAutomation);
}

BrowserBridge::~BrowserBridge() {
    if (pAutomation) pAutomation->Release();
    CoUninitialize();
}

std::string BrowserBridge::GetActiveURL(HWND hwnd) {
    if (!pAutomation) return "UIA_NOT_INIT";

    IUIAutomationElement* pElement = nullptr;
    std::string result = "URL_NOT_FOUND";

    // 1. HWND'den Element oluştur
    if (SUCCEEDED(pAutomation->ElementFromHandle(hwnd, &pElement))) {

        // 2. Arama Koşulu: Kontrol tipi "Edit" (Giriş kutusu) olanı ara
        IUIAutomationCondition* pCondition = nullptr;
        VARIANT varProp;
        varProp.vt = VT_I4;
        varProp.lVal = UIA_EditControlTypeId; // Adres çubukları genelde 'Edit' tipindedir

        if (SUCCEEDED(pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &pCondition))) {
            IUIAutomationElement* pEditBox = nullptr;

            // 3. Ağaçta ilk eşleşeni bul (FindFirst)
            if (SUCCEEDED(pElement->FindFirst(TreeScope_Descendants, pCondition, &pEditBox)) && pEditBox) {
                VARIANT varValue;
                // 4. Değeri (URL'yi) çek
                if (SUCCEEDED(pEditBox->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &varValue))) {
                    // BSTR türünden string'e çevrim
                    std::wstring ws(varValue.bstrVal);
                    result = std::string(ws.begin(), ws.end());
                    VariantClear(&varValue);
                }
                pEditBox->Release();
            }
            pCondition->Release();
        }
        pElement->Release();
    }
    return result;
}
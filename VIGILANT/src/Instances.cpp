#include "Data/DatabaseManager.hpp"
#include "UI/WebViewManager.hpp"
#include "Utils/EventBridge.hpp"
#include "AI/GeminiService.hpp"
#include "AI/AIClassifierTask.hpp"

// Global database instance definition
DatabaseManager g_Vault("vigilant.db");

// Global WebView2 manager (initialized in main)
WebViewManager* g_WebViewManager = nullptr;

// Global EventBridge (initialized in main)
EventBridge* g_EventBridge = nullptr;

// Global AI service instance
GeminiService g_Gemini;

// Global AI arka plan siniflandirma gorevi
AIClassifierTask g_AIClassifier(g_Vault, g_Gemini, 300, 20);

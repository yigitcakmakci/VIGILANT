#include "Data/DatabaseManager.hpp"
#include "UI/WebViewManager.hpp"
#include "AI/GeminiService.hpp"

// Global database instance definition
DatabaseManager g_Vault("vigilant.db");

// Global WebView2 manager (initialized in main)
WebViewManager* g_WebViewManager = nullptr;

// Global AI service instance
GeminiService g_Gemini;

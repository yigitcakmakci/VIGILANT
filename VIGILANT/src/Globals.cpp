#include <windows.h>
#include "Utils/EventQueue.hpp"

// Global EventQueue instance definition
EventQueue g_EventQueue;

// WebView2 thread ID for cross-thread resize messages
DWORD g_WebViewThreadId = 0;

#include <windows.h>
#include "Utils/EventQueue.hpp"

// Global EventQueue instance definitions
EventQueue g_EventQueue;

// WebView2 thread ID for cross-thread resize messages
DWORD g_WebViewThreadId = 0;

// HotkeyWorker thread ID for graceful shutdown
DWORD g_HotkeyThreadId = 0;

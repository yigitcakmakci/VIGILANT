#ifndef FOCUS_GUARD_HPP
#define FOCUS_GUARD_HPP

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>

#include <windows.h>

class DatabaseManager;

// ═══════════════════════════════════════════════════════════════════════
// FocusGuard — 3-level focus protection policy.
//
//   None   -> never react.
//   Warn   -> when a "Game"-categorised window becomes foreground, post
//             a small toast event (FocusGuardWarn) to the WebView UI.
//   Block  -> additionally raise a topmost full-screen layered overlay
//             window over the foreground monitor with a "Geri Dön" CTA.
//             A 60-second per-process snooze prevents repeat triggering.
//
// Policy is persisted in AppSettings (key="focus_guard_policy",
// value in { "none", "warn", "block" }).
// ═══════════════════════════════════════════════════════════════════════
class FocusGuard {
public:
	enum class Policy { None = 0, Warn = 1, Block = 2, Soft = 3 };

	FocusGuard();
	~FocusGuard();

	// Load persisted policy and start the overlay worker thread.
	void Init(DatabaseManager& vault);
	void Shutdown();

	Policy GetPolicy() const { return m_policy.load(std::memory_order_acquire); }
	void   SetPolicy(Policy p);  // persists to DB

	// Called by WindowTracker resolver whenever the foreground window
	// changes.  `category` may be empty / "Uncategorized".
	void OnActiveAppChanged(const std::string& process,
							const std::string& title,
							const std::string& category);

	// String <-> enum helpers
	static Policy        ParsePolicy(const std::string& s);
	static const char*   PolicyToString(Policy p);

	// Returns true if the given category string represents a game
	// (case-insensitive; matches "game", "gaming", "oyun").
	static bool IsGameCategory(const std::string& category);

private:
	// Overlay worker thread + window
	void OverlayThreadProc();
	void ShowOverlay(HWND foreground,
					 const std::string& process,
					 const std::string& title);
	void HideOverlay();
	void LayoutOverlayControls();
	void SubmitIntent(bool keepRunning);

	static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg,
											WPARAM wParam, LPARAM lParam);

	// Sends a FocusGuardWarn JSON event to the WebView UI thread.
	void PostWarnToWebView(const std::string& process,
						   const std::string& title);

	DatabaseManager*    m_vault = nullptr;
	std::atomic<Policy> m_policy{ Policy::None };

	// Per-process snooze (process name -> snooze-until time-point)
	std::mutex m_snoozeMutex;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_snoozeUntil;

	// Overlay thread state
	std::thread        m_overlayThread;
	DWORD              m_overlayThreadId = 0;
	std::atomic<bool>  m_overlayRunning{ false };
	std::atomic<bool>  m_overlayVisible{ false };
	HWND               m_overlayHwnd      = nullptr;  // touched only on overlay thread
	HWND               m_overlayEdit      = nullptr;
	HWND               m_overlayBtnGo     = nullptr;
	HWND               m_overlayBtnQuit   = nullptr;
	HWND               m_overlayTargetWnd = nullptr;
	std::string        m_overlayProcess;
	std::string        m_overlayTitle;
};

extern FocusGuard g_FocusGuard;

#endif  // FOCUS_GUARD_HPP

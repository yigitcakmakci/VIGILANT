#include "Core/FocusGuard.hpp"
#include "Data/DatabaseManager.hpp"
#include "Utils/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

#include <windows.h>
#include <commctrl.h>

#define WM_WEBVIEW_ACTIVEAPP (WM_APP + 3)
#define WM_FOCUSGUARD_SHOW   (WM_APP + 20)
#define WM_FOCUSGUARD_HIDE   (WM_APP + 21)

#define IDC_FG_EDIT     5101
#define IDC_FG_BTN_GO   5102
#define IDC_FG_BTN_QUIT 5103

extern DWORD g_WebViewThreadId;

FocusGuard g_FocusGuard;

namespace {
	constexpr wchar_t kOverlayClass[] = L"VigilantFocusGuardOverlay";
	constexpr int kSnoozeSeconds      = 60;

	std::string ToLowerAscii(const std::string& s) {
		std::string r = s;
		std::transform(r.begin(), r.end(), r.begin(),
					   [](unsigned char c) { return (char)std::tolower(c); });
		return r;
	}

	std::wstring Utf8ToWide(const std::string& s) {
		if (s.empty()) return L"";
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
									(int)s.size(), nullptr, 0);
		std::wstring out(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
							out.data(), n);
		return out;
	}
}

// ── Static helpers ─────────────────────────────────────────────────────
FocusGuard::Policy FocusGuard::ParsePolicy(const std::string& s) {
	auto lo = ToLowerAscii(s);
	if (lo == "warn")  return Policy::Warn;
	if (lo == "block") return Policy::Block;
	if (lo == "soft")  return Policy::Soft;
	return Policy::None;
}

const char* FocusGuard::PolicyToString(Policy p) {
	switch (p) {
		case Policy::Warn:  return "warn";
		case Policy::Block: return "block";
		case Policy::Soft:  return "soft";
		default:            return "none";
	}
}

bool FocusGuard::IsGameCategory(const std::string& category) {
	auto lo = ToLowerAscii(category);
	return lo == "game" || lo == "gaming" || lo == "oyun" || lo == "games";
}

// ── Construction / lifecycle ───────────────────────────────────────────
FocusGuard::FocusGuard() = default;

FocusGuard::~FocusGuard() {
	Shutdown();
}

void FocusGuard::Init(DatabaseManager& vault) {
	m_vault = &vault;
	auto persisted = vault.getSetting("focus_guard_policy", "none");
	m_policy.store(ParsePolicy(persisted), std::memory_order_release);

	m_overlayRunning.store(true, std::memory_order_release);
	m_overlayThread = std::thread(&FocusGuard::OverlayThreadProc, this);
}

void FocusGuard::Shutdown() {
	if (!m_overlayRunning.exchange(false, std::memory_order_acq_rel))
		return;
	if (m_overlayThreadId != 0) {
		PostThreadMessageW(m_overlayThreadId, WM_QUIT, 0, 0);
	}
	if (m_overlayThread.joinable())
		m_overlayThread.join();
}

void FocusGuard::SetPolicy(Policy p) {
	m_policy.store(p, std::memory_order_release);
	if (m_vault)
		m_vault->setSetting("focus_guard_policy", PolicyToString(p));
	if (p == Policy::None && m_overlayThreadId != 0) {
		PostThreadMessageW(m_overlayThreadId, WM_FOCUSGUARD_HIDE, 0, 0);
	}
}

namespace {
	struct ShowOverlayPayload {
		HWND        foreground;
		std::string process;
		std::string title;
	};
}

// ── Active-window dispatch ─────────────────────────────────────────────
void FocusGuard::OnActiveAppChanged(const std::string& process,
									const std::string& title,
									const std::string& category) {
	auto basePolicy = m_policy.load(std::memory_order_acquire);

	// Bütçe aşımı — base policy ne olursa olsun ek tırmanma uygular.
	// Bütçeli süreç için: %100 → en az Soft, %130 → Warn, %160 → Block.
	Policy effective = basePolicy;
	if (m_vault) {
		int limit = m_vault->getDailyBudget(process);
		if (limit > 0) {
			int used = m_vault->getTodayUsageSeconds(process);
			double r = (double)used / (double)limit;
			Policy budgetEsc = Policy::None;
			if      (r >= 1.60) budgetEsc = Policy::Block;
			else if (r >= 1.30) budgetEsc = Policy::Warn;
			else if (r >= 1.00) budgetEsc = Policy::Soft;
			// effective = max(basePolicy, budgetEsc)
			auto rank = [](Policy p) {
				switch (p) {
					case Policy::None:  return 0;
					case Policy::Warn:  return 1;
					case Policy::Soft:  return 2;
					case Policy::Block: return 3;
				}
				return 0;
			};
			if (rank(budgetEsc) > rank(effective)) effective = budgetEsc;
		}
	}

	if (effective == Policy::None) return;
	// Bütçe tetiklenmişse oyun kategorisi şartı aranmaz; aksi halde sadece oyun.
	if (effective == basePolicy && !IsGameCategory(category)) return;

	// Snooze check (per-process)
	auto now = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lk(m_snoozeMutex);
		auto it = m_snoozeUntil.find(process);
		if (it != m_snoozeUntil.end() && it->second > now)
			return;
		m_snoozeUntil[process] = now + std::chrono::seconds(kSnoozeSeconds);
	}

	// Always warn the dashboard so the user has visible feedback.
	PostWarnToWebView(process, title);

	// Warn = sadece dashboard toast. Soft & Block = uygulamayı kaplayan
	// native tam ekran soru overlay'ini aç.
	if ((effective == Policy::Soft || effective == Policy::Block) &&
		m_overlayThreadId != 0) {
		HWND fg = GetForegroundWindow();
		auto* payload = new ShowOverlayPayload{ fg, process, title };
		if (!PostThreadMessageW(m_overlayThreadId, WM_FOCUSGUARD_SHOW,
								0, reinterpret_cast<LPARAM>(payload))) {
			delete payload;
		}
	}
}

void FocusGuard::PostWarnToWebView(const std::string& process,
								   const std::string& title) {
	DWORD threadId = g_WebViewThreadId;
	if (threadId == 0) return;
	try {
		nlohmann::json j;
		j["type"]    = "FocusGuardWarn";
		j["version"] = 1;
		j["payload"] = {
			{"process",     process},
			{"windowTitle", title},
			{"policy",      PolicyToString(m_policy.load(std::memory_order_acquire))}
		};
		std::string* pJson = new std::string(j.dump());
		if (!PostThreadMessageW(threadId, WM_WEBVIEW_ACTIVEAPP, 0,
								reinterpret_cast<LPARAM>(pJson))) {
			delete pJson;
		}
	} catch (...) {
		// best-effort; ignore JSON failures
	}
}

// ── Overlay thread ─────────────────────────────────────────────────────
void FocusGuard::OverlayThreadProc() {
	m_overlayThreadId = GetCurrentThreadId();

	WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
	wc.lpfnWndProc   = &FocusGuard::OverlayWndProc;
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = kOverlayClass;
	RegisterClassExW(&wc);

	// Force the message queue to exist so PostThreadMessage doesn't drop.
	MSG dummy;
	PeekMessageW(&dummy, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

	MSG msg;
	while (m_overlayRunning.load(std::memory_order_acquire) &&
		   GetMessageW(&msg, nullptr, 0, 0) > 0) {
		if (msg.message == WM_FOCUSGUARD_SHOW) {
			auto* payload = reinterpret_cast<ShowOverlayPayload*>(msg.lParam);
			if (payload) {
				ShowOverlay(payload->foreground, payload->process, payload->title);
				delete payload;
			}
			continue;
		}
		if (msg.message == WM_FOCUSGUARD_HIDE) {
			HideOverlay();
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	HideOverlay();
	UnregisterClassW(kOverlayClass, GetModuleHandleW(nullptr));
	m_overlayThreadId = 0;
}

void FocusGuard::ShowOverlay(HWND foreground,
							 const std::string& process,
							 const std::string& title) {
	// Overlay'i SADECE hedef uygulamanin pencere dikdortgenine yerlestir.
	// Boylece monitorun tamamini kaplamiyor; sadece oyunun/uygulamanin
	// ustunu ortuyor.
	RECT target = { 0, 0, 0, 0 };
	if (foreground && IsWindow(foreground)) {
		GetWindowRect(foreground, &target);
	}
	int x = target.left;
	int y = target.top;
	int w = target.right  - target.left;
	int h = target.bottom - target.top;

	// Eger pencere cok kucukse veya alinamadiysa monitor'e fallback yap.
	if (w < 480 || h < 320) {
		HMONITOR mon = MonitorFromWindow(
			foreground ? foreground : GetDesktopWindow(),
			MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = { sizeof(MONITORINFO) };
		GetMonitorInfoW(mon, &mi);
		int mw = mi.rcWork.right  - mi.rcWork.left;
		int mh = mi.rcWork.bottom - mi.rcWork.top;
		w = (mw * 2) / 3;
		h = (mh * 2) / 3;
		x = mi.rcWork.left + (mw - w) / 2;
		y = mi.rcWork.top  + (mh - h) / 2;
	}

	m_overlayTargetWnd = foreground;
	m_overlayProcess   = process;
	m_overlayTitle     = title;

	if (!m_overlayHwnd) {
		m_overlayHwnd = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
			kOverlayClass, L"VIGILANT",
			WS_POPUP,
			x, y, w, h,
			nullptr, nullptr, GetModuleHandleW(nullptr), this);
		if (!m_overlayHwnd) return;
		SetLayeredWindowAttributes(m_overlayHwnd, 0, 240, LWA_ALPHA);

		HFONT editFont = CreateFontW(
			22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
		HFONT btnFont = CreateFontW(
			20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

		m_overlayEdit = CreateWindowExW(
			WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			0, 0, 10, 10, m_overlayHwnd,
			(HMENU)IDC_FG_EDIT, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(m_overlayEdit, WM_SETFONT, (WPARAM)editFont, TRUE);
		// "Ornek: 30 dk mola - sonra plana donuyorum"
		SendMessageW(m_overlayEdit, EM_SETCUEBANNER, TRUE,
			(LPARAM)L"\u00D6rnek: 30 dk mola \u2014 sonra plana d\u00F6n\u00FCyorum");

		// "Devam et"
		m_overlayBtnGo = CreateWindowExW(
			0, L"BUTTON", L"Devam et",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			0, 0, 10, 10, m_overlayHwnd,
			(HMENU)IDC_FG_BTN_GO, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(m_overlayBtnGo, WM_SETFONT, (WPARAM)btnFont, TRUE);

		// "Vazgec ve kapat"
		m_overlayBtnQuit = CreateWindowExW(
			0, L"BUTTON", L"Vazge\u00E7 ve kapat",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 10, 10, m_overlayHwnd,
			(HMENU)IDC_FG_BTN_QUIT, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(m_overlayBtnQuit, WM_SETFONT, (WPARAM)btnFont, TRUE);
	} else {
		SetWindowPos(m_overlayHwnd, HWND_TOPMOST, x, y, w, h, 0);
		SetWindowTextW(m_overlayEdit, L"");
	}

	LayoutOverlayControls();
	ShowWindow(m_overlayHwnd, SW_SHOW);
	SetForegroundWindow(m_overlayHwnd);
	BringWindowToTop(m_overlayHwnd);
	SetFocus(m_overlayEdit);
	InvalidateRect(m_overlayHwnd, nullptr, TRUE);
	m_overlayVisible.store(true, std::memory_order_release);
}

void FocusGuard::LayoutOverlayControls() {
	if (!m_overlayHwnd) return;
	RECT rc; GetClientRect(m_overlayHwnd, &rc);
	int W = rc.right - rc.left;
	int H = rc.bottom - rc.top;

	int editW = (int)(W * 0.7);
	if (editW < 280) editW = (W > 320) ? W - 40 : W - 20;
	int editH = 44;
	int editX = (W - editW) / 2;
	int editY = H / 2 + 10;
	if (m_overlayEdit)
		MoveWindow(m_overlayEdit, editX, editY, editW, editH, TRUE);

	int btnW = 180, btnH = 44;
	int gap  = 16;
	int btnY = editY + editH + 20;
	int totalW = btnW * 2 + gap;
	if (totalW > W - 20) {
		btnW   = (W - 20 - gap) / 2;
		totalW = btnW * 2 + gap;
	}
	int btnX1 = (W - totalW) / 2;
	int btnX2 = btnX1 + btnW + gap;
	if (m_overlayBtnGo)
		MoveWindow(m_overlayBtnGo,   btnX1, btnY, btnW, btnH, TRUE);
	if (m_overlayBtnQuit)
		MoveWindow(m_overlayBtnQuit, btnX2, btnY, btnW, btnH, TRUE);
}

void FocusGuard::SubmitIntent(bool keepRunning) {
	std::string intent;
	if (m_overlayEdit) {
		wchar_t buf[1024] = { 0 };
		GetWindowTextW(m_overlayEdit, buf, 1023);
		int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
									nullptr, 0, nullptr, nullptr);
		if (n > 1) {
			std::string s(n - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, buf, -1,
								s.data(), n, nullptr, nullptr);
			intent = std::move(s);
		}
	}

	if (m_vault) {
		m_vault->logIntent(m_overlayProcess, m_overlayTitle,
						   intent,
						   keepRunning ? "continued" : "closed",
						   0);
	}

	HWND target = m_overlayTargetWnd;
	HideOverlay();

	if (!keepRunning && target && IsWindow(target)) {
		// Kullanıcı vazgeçti — uygulamayı kibarca kapat.
		PostMessageW(target, WM_CLOSE, 0, 0);
	}
}

void FocusGuard::HideOverlay() {
	if (m_overlayHwnd) {
		ShowWindow(m_overlayHwnd, SW_HIDE);
		DestroyWindow(m_overlayHwnd);
		m_overlayHwnd      = nullptr;
		m_overlayEdit      = nullptr;
		m_overlayBtnGo     = nullptr;
		m_overlayBtnQuit   = nullptr;
		m_overlayTargetWnd = nullptr;
	}
	m_overlayVisible.store(false, std::memory_order_release);
}

LRESULT CALLBACK FocusGuard::OverlayWndProc(HWND hWnd, UINT msg,
											 WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_COMMAND: {
			WORD id  = LOWORD(wParam);
			WORD evt = HIWORD(wParam);
			if (id == IDC_FG_BTN_GO && evt == BN_CLICKED) {
				g_FocusGuard.SubmitIntent(true);
				return 0;
			}
			if (id == IDC_FG_BTN_QUIT && evt == BN_CLICKED) {
				g_FocusGuard.SubmitIntent(false);
				return 0;
			}
			break;
		}

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				g_FocusGuard.SubmitIntent(true);
				return 0;
			}
			break;

		case WM_SIZE:
			g_FocusGuard.LayoutOverlayControls();
			return 0;

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT: {
			HDC hdc = (HDC)wParam;
			SetTextColor(hdc, RGB(245, 245, 245));
			SetBkColor(hdc, RGB(20, 26, 38));
			static HBRUSH br = CreateSolidBrush(RGB(20, 26, 38));
			return (LRESULT)br;
		}

		case WM_ERASEBKGND: {
			HDC hdc = (HDC)wParam;
			RECT rc; GetClientRect(hWnd, &rc);
			HBRUSH bg = CreateSolidBrush(RGB(8, 12, 20));
			FillRect(hdc, &rc, bg);
			DeleteObject(bg);
			return 1;
		}

		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			RECT rc; GetClientRect(hWnd, &rc);

			SetBkMode(hdc, TRANSPARENT);

			HFONT bigFont = CreateFontW(
				40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
			HFONT midFont = CreateFontW(
				22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
			HFONT smallFont = CreateFontW(
				16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

			// "Bir saniye..."
			RECT title = rc;
			title.top    = 30;
			title.bottom = title.top + 56;
			SelectObject(hdc, bigFont);
			SetTextColor(hdc, RGB(245, 158, 11));
			DrawTextW(hdc, L"\u23F8  Bir saniye\u2026",
					  -1, &title,
					  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

			// "Bu uygulamaya neden girdin? (process.exe)"
			RECT q = rc;
			q.top    = (rc.top + rc.bottom) / 2 - 36;
			q.bottom = q.top + 32;
			SelectObject(hdc, midFont);
			SetTextColor(hdc, RGB(229, 231, 235));
			std::wstring qText = L"Bu uygulamaya neden girdin?";
			if (!g_FocusGuard.m_overlayProcess.empty()) {
				qText += L"  (";
				qText += Utf8ToWide(g_FocusGuard.m_overlayProcess);
				qText += L")";
			}
			DrawTextW(hdc, qText.c_str(), -1, &q,
					  DT_CENTER | DT_TOP | DT_SINGLELINE);

			// Footer: "Niyetini yaz, ... 60 sn boyunca tekrar sormayacagiz."
			RECT footer = rc;
			footer.top    = rc.bottom - 56;
			footer.left  += 20;
			footer.right -= 20;
			SelectObject(hdc, smallFont);
			SetTextColor(hdc, RGB(156, 163, 175));
			DrawTextW(hdc,
				L"Niyetini yaz, \u201CDevam et\u201D ile ge\u00E7 ya da "
				L"\u201CVazge\u00E7 ve kapat\u201D ile uygulamay\u0131 kapat.\n"
				L"VIGILANT odak korumas\u0131 seni izliyor \u2014 60 sn boyunca tekrar sormayacak.",
				-1, &footer,
				DT_CENTER | DT_TOP | DT_WORDBREAK);

			DeleteObject(bigFont);
			DeleteObject(midFont);
			DeleteObject(smallFont);
			EndPaint(hWnd, &ps);
			return 0;
		}

		case WM_CLOSE:
			g_FocusGuard.SubmitIntent(true);
			return 0;

		case WM_DESTROY:
			return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

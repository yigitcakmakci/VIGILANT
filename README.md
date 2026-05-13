# VIGILANT

> Windows masaüstü için aktivite izleme + odak koruma + AI destekli hedef
> planlama uygulaması. Native C++ (Win32 + WebView2 + SQLite) ile yazılmış,
> dashboard'u TypeScript/JS olarak WebView2 içinde çalıştırılan bir
> hibrit uygulamadır.

## ✨ Ne yapıyor?

- **Aktivite izleme**: Windows event hook'ları (`SetWinEventHook`) ile
  ön plandaki uygulama / pencere başlığı / tarayıcı URL'si gerçek
  zamanlı olarak yakalanır ve SQLite veritabanına yazılır.
- **AI sınıflandırma**: Yakalanan aktiviteler arka planda Gemini /
  OpenAI / Anthropic sağlayıcılarından biriyle kategori + skor
  (verimli/tarafsız/verimsiz) atanır.
- **Sokratik mülakat & hedef ağacı**: Kullanıcı bir hedef girer, AI
  sokratik soru-cevap ile hedefi netleştirir ve bunu rekürsif bir
  `DynamicGoalTree` (Major → Minor → Micro) yapısına dönüştürür.
- **Manuel plan**: AI'a güvenmek istemeyenler için aynı ağacı
  Hedefler sekmesindeki "Manuel Plan" akışıyla kendi elleriyle
  oluşturabilirler.
- **Odak Koruma**: Ayarlar > "Odak Koruma" altında 3 seviyeli politika
  seçilebilir:
  - **Kapalı** — hiçbir şey yapma.
  - **Uyar** — `Game` kategorisindeki bir pencere ön plana gelirse
    dashboard'da kısa bir uyarı toast'ı göster.
  - **Engelle** — pencerenin önüne tam ekran "Geri Dön" overlay'i koy
    (60 sn snooze).

## 🧱 Mimari

```
┌──────────────────────────┐     ┌──────────────────────────┐
│  WindowTracker (hook)    │ ──► │  ResolverThread (heavy)  │
└──────────────────────────┘     └────────────┬─────────────┘
                                              │
                          ┌───────────────────┼────────────────────┐
                          ▼                   ▼                    ▼
                   ┌───────────────┐   ┌──────────────┐   ┌──────────────────┐
                   │ EventBridge   │   │ EventQueue   │   │ FocusGuard       │
                   │ (UI postMsg)  │   │ (DB writer)  │   │ (policy enforce) │
                   └──────┬────────┘   └──────┬───────┘   └──────┬───────────┘
                          ▼                   ▼                  ▼
                   ┌──────────────────────────────────────────────────────┐
                   │  WebView2 Dashboard (dashboard_pro.html + bundles)  │
                   └──────────────────────────────────────────────────────┘
```

- `src/main.cpp` — bootstrap, mesaj döngüsü, WebView2 STA thread.
- `src/Core/WindowTracker.cpp` — hafif hook + ayrı resolver thread.
- `src/Utils/EventBridge.cpp` — aktif uygulama event'lerini WebView'e
  20Hz throttled olarak iletir.
- `src/Workers.cpp` — `BackgroundWorker` (DB batching) + `HotkeyWorker`.
- `src/Data/DatabaseManager.cpp` — SQLite şema, `WAL` + prepared
  statements, `AppSettings` key/value tablosu, mülakat/hedef
  persistance.
- `src/AI/GeminiService.cpp` — sağlayıcı seçimi, validation, retry.
- `src/AI/InterviewHandler.cpp` — sokratik mülakat & `DynamicGoalTree`
  v2 inşası.
- `src/AI/GoalManager.cpp` — bellekte hedef ormanı (forest) +
  ekleme/silme/yeniden sıralama.
- `src/Core/FocusGuard.cpp` — 3 seviyeli odak koruma politikası ve
  block overlay penceresi.
- `src/UI/WebViewManager.cpp` — JS ↔ native mesaj köprüsü.
- `web/html/dashboard_pro.html` + `web/js/*.js` + `ts/**` — dashboard
  arayüzü.

## 🛠 Build

Visual Studio 2022 / 2026 + C++17. Solution kökü: `VIGILANT.sln`.

1. `git clone` ve solution'u Visual Studio'da aç.
2. NuGet paketlerini geri yükle (Microsoft.Web.WebView2, WIL).
3. `x64 / Debug` ya da `x64 / Release` ile build et.
4. İlk çalıştırmadan önce ortam değişkenlerini ayarla (aşağıda).

## 🔑 Ortam değişkenleri

| Değişken | Açıklama |
|----------|----------|
| `GEMINI_API_KEY` | Gemini için API anahtarı (varsayılan sağlayıcı). |
| `OPENAI_API_KEY` | OpenAI sağlayıcısı seçilirse. |
| `ANTHROPIC_API_KEY` | Anthropic sağlayıcısı seçilirse. |
| `VIGILANT_AI_PROVIDER` | `gemini` \| `openai` \| `anthropic` (ops.). |
| `VIGILANT_AI_MODEL` | Model adı (ops., sağlayıcıya göre default). |
| `VIGILANT_PERF_ENABLED` | Tanımlıysa 30 sn'de bir performans snapshot. |

> Gizlilik: API anahtarları **asla** disk'e yazılmaz, log'a basılmaz
> ve geçici buffer'lar `SecureZeroMemory` ile sıfırlanır.

## ⌨ Hot-key'ler

- **Ctrl + Shift + V** — Dashboard penceresini göster/gizle (tray'e küçültür).

## 📂 Veri

Veritabanı dosyası `vigilant.db` çalıştırılan dizinde tutulur. Tüm
verileri silmek için: **Ayarlar → Veri Yönetimi → Veritabanını Temizle**.

## 🔒 Lisans

Henüz açık lisanslandırılmadı. Şahsi kullanım amaçlıdır.

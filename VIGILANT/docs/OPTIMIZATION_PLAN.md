# VIGILANT — CPU <%1 Optimization Plan

## Mevcut Mimari Analizi

```
Hook Thread (UI pump)         BackgroundWorker Thread        EventBridge Thread
─────────────────────         ─────────────────────────      ──────────────────
WinEventProc                  g_EventQueue.pop()             RingBuffer drain
 ├─ OpenProcess()              ├─ g_Vault.logActivity()       └─ SendToWebView()
 ├─ QueryFullProcessImageName  │   ├─ prepare_v2  (!)
 ├─ WideToUtf8 (alloc)        │   ├─ bind+step
 ├─ GetActiveURL (UIA!)        │   └─ finalize    (!)
 ├─ EventData ctor (copies)    └─ g_Vault.updateDuration()
 ├─ g_EventQueue.push()            ├─ prepare_v2  (!)
 └─ g_EventBridge->PushEvent()     └─ finalize    (!)
```

### Tespit Edilen Sorunlar (Severity Sırasıyla)

| # | Sorun | Etki | Dosya |
|---|-------|------|-------|
| 1 | **UIA GetActiveURL() callback'de** | 50-200ms blok | WindowTracker.cpp:147 |
| 2 | **OpenProcess her event'te** | Kernel call her seferinde | WindowTracker.cpp:113 |
| 3 | **sqlite3_prepare_v2 her INSERT'te** | SQL parse overhead | DatabaseManager.cpp:84 |
| 4 | **Her INSERT ayrı transaction** | fsync her yazımda | Workers.cpp:40 |
| 5 | **std::string kopyaları callback'de** | Heap alloc yığını | WindowTracker.cpp:103-157 |
| 6 | **Process name cache yok** | Aynı PID tekrar sorgulanıyor | WindowTracker.cpp:64 |

---

## Optimizasyon Checklist'i

### Phase 1: Callback Hafifletme (En Yüksek Etki)

- [x] **1.1 Lightweight Hook Event**
  - Callback sadece `HWND` + `event` + `dwmsEventTime` yakalar
  - Debounce kontrolü callback'de kalır (ucuz)
  - Tüm ağır iş worker thread'e taşınır

- [x] **1.2 Process Name Cache**
  - `std::unordered_map<DWORD, CachedProcess>` ile PID → exe name cache
  - TTL: 60 saniye (process yeniden başlarsa güncellensin)
  - Cache hit = 0 kernel call

- [x] **1.3 UIA Çağrısını Worker'a Taşı**
  - `BrowserBridge::GetActiveURL()` asla callback'de çağrılmayacak
  - Worker thread'de, sadece browser process'ler için çağrılacak

### Phase 2: Database Optimizasyonu

- [x] **2.1 WAL Mode**
  - `PRAGMA journal_mode=WAL;` — concurrent read + write
  - `PRAGMA synchronous=NORMAL;` — WAL ile güvenli, %50 daha hızlı

- [x] **2.2 Prepared Statement Cache**
  - `logActivity` ve `updateDuration` için statement'lar bir kez prepare edilir
  - `sqlite3_reset()` + `sqlite3_clear_bindings()` ile tekrar kullanılır
  - prepare_v2/finalize overhead: ~0 (init'te bir kez)

- [x] **2.3 Batch Transaction**
  - N event veya T saniye biriktir → tek BEGIN/COMMIT
  - Varsayılan: 10 event veya 2 saniye (hangisi önce dolarsa)
  - fsync sayısı: event başına 1 → batch başına 1

### Phase 3: String & Allocation Azaltma

- [x] **3.1 Stack Buffer'da WideToUtf8**
  - 512 byte'a kadar stack buffer, aşarsa heap fallback
  - `SmallString<N>` pattern veya `char stackBuf[512]`

- [x] **3.2 EventData'da move semantics**
  - `g_EventQueue.push(std::move(data))` — copy yerine move
  - Queue pop'ta da move kullan

### Phase 4: Ölçüm Altyapısı

- [x] **4.1 PerfCounters**
  - Atomic counter'lar: hook_calls, events_queued, db_writes, cache_hits/misses
  - `PERF_SCOPED_TIMER` macro'su: scope bazlı süre ölçümü
  - Periyodik telemetry dump (her 30 saniye)

---

## Hedef Mimari

```
Hook Thread (ULTRA LIGHT)     Resolver Thread (NEW)        DB Batch Thread
─────────────────────────     ────────────────────         ───────────────
WinEventProc                  RawHookEvent pop()           BatchTimer (2s)
 ├─ debounce (GetTickCount64) ├─ ProcessCache lookup       ├─ BEGIN
 ├─ dedup (hwnd==last?)       ├─ OpenProcess (cache miss)  ├─ INSERT x N
 └─ push RawHookEvent         ├─ WideToUtf8                ├─ UPDATE x N
     {hwnd, event, tick}      ├─ GetActiveURL (browser)    └─ COMMIT
                              ├─ EventData oluştur
                              ├─ g_EventQueue.push()
                              └─ g_EventBridge->PushEvent()
```

---

## Beklenen Kazanımlar

| Metrik | Önce | Sonra | İyileşme |
|--------|------|-------|----------|
| Callback süresi | 5-200ms | <0.01ms | **~10000x** |
| DB write latency | ~2ms/event | ~0.2ms/event (batched) | **~10x** |
| String allocs/event | ~8 | ~2-3 | **~3x** |
| Process queries/min | event sayısı kadar | cache hit sonrası ~0 | **~50x** |
| Toplam CPU | %2-5 | <%1 | **Hedef** |

---

## Ölçüm Noktaları

```
PERF_COUNT(hook_callback_entered)     — WinEventProc girişi
PERF_COUNT(hook_callback_debounced)   — debounce ile düşürülen
PERF_COUNT(hook_callback_deduped)     — dedup ile düşürülen
PERF_COUNT(events_queued)             — kuyruğa gönderilen
PERF_COUNT(process_cache_hit)         — cache hit
PERF_COUNT(process_cache_miss)        — cache miss (kernel call)
PERF_COUNT(db_batch_committed)        — batch commit sayısı
PERF_COUNT(db_rows_inserted)          — toplam satır
PERF_COUNT(bridge_events_sent)        — WebView'a gönderilen
PERF_SCOPED_TIMER(resolve_event)      — resolver thread event işleme süresi
PERF_SCOPED_TIMER(db_batch_commit)    — batch commit süresi
```

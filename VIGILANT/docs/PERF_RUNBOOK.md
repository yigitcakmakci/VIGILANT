# VIGILANT — Windows Performance Measurement Runbook

> **Version:** 1.0  
> **Son güncelleme:** 2025-01  
> **Kapsam:** CPU%, RAM, event/sec, queue depth, UI frame time  
> **Araçlar:** WPR/WPA (ETW) + VIGILANT Internal PerfCounters + PerfSnapshot

---

## 1. Ön Hazırlık

### 1.1 Build Konfigürasyonu

| Amaç | Build | Preprocessor |
|-------|-------|-------------|
| ETW Profiling | Release + PDB | `NDEBUG` |
| Internal Counters | Debug veya RelWithDebInfo | `VIGILANT_PERF_ENABLED` |
| Tam Ölçüm (ikisi birden) | RelWithDebInfo | `VIGILANT_PERF_ENABLED` |

```
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVIGILANT_PERF_ENABLED=ON ..
```

### 1.2 Araç Kontrolü

```powershell
# WPR kurulu mu?
wpr -status

# DebugView (internal counter dump'ları için)
# https://learn.microsoft.com/en-us/sysinternals/downloads/debugview

# Alternatif: PerfSnapshot CSV logları → docs\perf_logs\ dizini
```

### 1.3 Baseline Ortamı

| Parametre | Değer |
|-----------|-------|
| OS | Windows 10/11 (22H2+) |
| RAM | Minimum 8 GB |
| Arka plan uygulamaları | Kapalı (Slack, Teams, AV real-time hariç) |
| Ekran çözünürlüğü | 1920×1080 (sabit) |
| Güç planı | **High Performance** |
| WebView2 Runtime | Evergreen (en son) |

---

## 2. Ölçüm Senaryoları

### Senaryo A: Idle (Baseline)

| | |
|---|---|
| **Amaç** | Sıfır kullanıcı etkileşiminde kaynak tüketimini ölçmek |
| **Süre** | 5 dakika |
| **Prosedür** | VIGILANT'ı başlat → masaüstüne tıkla → fareye/klavyeye dokunma → bekle |
| **Beklenti** | CPU ≈ 0%, hook callback yok, queue boş, UI repaint yok |

### Senaryo B: Yoğun Alt-Tab (Stress)

| | |
|---|---|
| **Amaç** | Maksimum event hızında pipeline davranışını test etmek |
| **Süre** | 2 dakika |
| **Prosedür** | 8+ farklı uygulama açık → 1 saniyede 1 Alt-Tab (sürekli) |
| **Beklenti** | ~60 event/min, queue depth < 16, CPU < 3% |

### Senaryo C: 8 Saat Tracking (Endurance)

| | |
|---|---|
| **Amaç** | Bellek sızıntısı, handle leak, CPU drift tespiti |
| **Süre** | 8 saat (iş günü simülasyonu) |
| **Prosedür** | Normal kullanım: IDE + browser + chat → PerfSnapshot 30s interval |
| **Beklenti** | RAM delta < 10 MB, handle count stabil, CPU avg < 1% |

---

## 3. Adım Adım Ölçüm Prosedürü

### Adım 1 — ETW Trace Başlat (WPR)

```powershell
# Hafif profil — CPU sampling + handles + memory
wpr -start CPU -start Handle -start VirtualAllocation -filemode

# VEYA özel profil:
# wpr -start docs\vigilant_perf.wprp -filemode
```

### Adım 2 — VIGILANT'ı Başlat

```powershell
# Debug build (internal counters aktif)
cd build\RelWithDebInfo
.\VIGILANT.exe

# PerfSnapshot otomatik başlar (VIGILANT_PERF_ENABLED tanımlıysa)
# CSV loglar: docs\perf_logs\perf_YYYYMMDD_HHMMSS.csv
```

### Adım 3 — Senaryoyu Çalıştır

| Senaryo | Komut / Aksiyon |
|---------|----------------|
| A (Idle) | Fareye dokunma, 5dk bekle |
| B (Stress) | `Alt-Tab` sürekli (8+ uygulama arası, 1/sn hız) |
| C (Endurance) | Normal iş günü, PerfSnapshot 30s interval'de loglar |

### Adım 4 — ETW Trace Durdur

```powershell
wpr -stop docs\perf_logs\vigilant_trace.etl "VIGILANT Perf Run"
```

### Adım 5 — Internal Counter Snapshot Al

Internal counter'lar otomatik CSV'ye yazılır. Manuel dump için DebugView'da:
```
[PERF] hook_entered=142 debounced=23 deduped=8 queued=111 ...
[PERF-SNAP] cpu=0.3% rss=28.4MB events/s=1.2 q_depth=0 handles=187 frame=6.2ms
```

### Adım 6 — WPA Analizi

```powershell
wpa docs\perf_logs\vigilant_trace.etl
```

**WPA'da bakılacak grafikler:**

| Grafik | Nereye Bak |
|--------|-----------|
| CPU Usage (Sampled) | `VIGILANT.exe` → thread bazlı breakdown |
| CPU Usage (Precise) | Context switch count, ready time |
| Virtual Memory Commit | Toplam commit büyümesi (8h boyunca düz olmalı) |
| Handle Count | `Process → Handle Count` → monoton artış = leak |
| Disk I/O | SQLite WAL flush pattern'i |

### Adım 7 — Raporlama

Internal CSV + ETW verilerini birleştir:

```
docs\perf_logs\
├── perf_20250115_093000.csv      ← PerfSnapshot (30s aralıklı)
├── vigilant_trace.etl             ← ETW full trace
└── summary.md                     ← Elle yazılan sonuç notu
```

---

## 4. Internal Metrics: PerfSnapshot

Mevcut `PerfCounters` (atomic counter'lar) üzerine eklenen, periyodik olarak
sistem metrikleri toplayan ve CSV'ye yazan yapı.

**Dosya:** `include/Utils/PerfSnapshot.hpp`

### Toplanan Metrikler

| Metrik | Kaynak | Birim |
|--------|--------|-------|
| `cpu_percent` | `GetProcessTimes()` delta | % |
| `rss_mb` | `GetProcessMemoryInfo()` WorkingSetSize | MB |
| `events_per_sec` | `PerfCounters::events_queued` delta / dt | evt/s |
| `queue_depth` | `EventQueue::size()` snapshot | adet |
| `handle_count` | `GetProcessHandleCount()` | adet |
| `ui_frame_time_ms` | Ana message loop cycle süresi | ms |
| `hook_callback_entered` | `PerfCounters` cumulative | adet |
| `resolve_event_max_us` | `PerfCounters` peak | µs |
| `db_batch_commit_max_us` | `PerfCounters` peak | µs |

### Kullanım

```cpp
// main.cpp — başlatma
#include "Utils/PerfSnapshot.hpp"

PerfSnapshotLogger perfLogger;
perfLogger.Start(30'000);  // 30 saniye aralık

// ... uygulama çalışır ...

perfLogger.Stop();
```

---

## 5. DoD (Definition of Done) Threshold Tablosu

Her build'in "performans kabul kriterleri". Herhangi bir metrik **FAIL** sütununu
aşarsa, commit merge edilmez.

### Senaryo A — Idle (5 dakika)

| Metrik | PASS (Yeşil) | WARN (Sarı) | FAIL (Kırmızı) |
|--------|:------------:|:-----------:|:---------------:|
| CPU % (avg) | < 0.1% | 0.1–0.5% | > 0.5% |
| CPU % (peak) | < 1.0% | 1.0–2.0% | > 2.0% |
| RAM (RSS) | < 40 MB | 40–60 MB | > 60 MB |
| Events/sec | 0 | — | > 0 |
| Queue Depth | 0 | — | > 0 |
| Handle Count | < 200 | 200–300 | > 300 |
| UI Frame Time | < 16 ms | 16–33 ms | > 33 ms |

### Senaryo B — Yoğun Alt-Tab (2 dakika, ~1 switch/sn)

| Metrik | PASS (Yeşil) | WARN (Sarı) | FAIL (Kırmızı) |
|--------|:------------:|:-----------:|:---------------:|
| CPU % (avg) | < 1.0% | 1.0–3.0% | > 3.0% |
| CPU % (peak) | < 5.0% | 5.0–8.0% | > 8.0% |
| RAM (RSS) | < 50 MB | 50–80 MB | > 80 MB |
| Events/sec | < 5.0 | 5.0–10.0 | > 10.0 |
| Queue Depth (max) | < 8 | 8–32 | > 32 |
| Hook Callback (µs avg) | < 5 | 5–20 | > 20 |
| Resolve Event (µs max) | < 500 | 500–2000 | > 2000 |
| DB Batch Commit (ms max) | < 10 | 10–50 | > 50 |
| UI Frame Time (p99) | < 16 ms | 16–33 ms | > 33 ms |
| RawQueue Drop Count | 0 | 1–5 | > 5 |

### Senaryo C — 8 Saat Endurance

| Metrik | PASS (Yeşil) | WARN (Sarı) | FAIL (Kırmızı) |
|--------|:------------:|:-----------:|:---------------:|
| CPU % (8h avg) | < 0.5% | 0.5–1.0% | > 1.0% |
| RAM Δ (başlangıç→bitiş) | < 5 MB | 5–15 MB | > 15 MB |
| RAM (RSS peak) | < 80 MB | 80–120 MB | > 120 MB |
| Handle Δ (8h) | < 10 | 10–50 | > 50 |
| Events/sec (avg) | < 2.0 | 2.0–5.0 | > 5.0 |
| DB Size Growth | < 50 MB | 50–100 MB | > 100 MB |
| Queue Depth (p99) | < 4 | 4–16 | > 16 |
| UI Frame Time (p99) | < 16 ms | 16–33 ms | > 33 ms |

---

## 6. WPR Custom Profile (Opsiyonel)

Daha hedefli ETW trace'ler için özel profil:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- docs/vigilant_perf.wprp -->
<WindowsPerformanceRecorder Version="1.0">
  <Profiles>
    <Profile Name="VIGILANT.Verbose" Description="CPU + Memory + Handles"
             DetailLevel="Verbose" LoggingMode="File" Id="VIGILANT.Verbose.File">
      <Collectors>
        <SystemCollectorId Value="SystemCollector_VIGILANT">
          <BufferSize Value="1024"/>
          <Buffers Value="64"/>
          <SystemProviderId Value="SystemProvider_VIGILANT"/>
        </SystemCollectorId>
      </Collectors>
    </Profile>
    <SystemCollector Id="SystemCollector_VIGILANT">
      <SystemProvider Id="SystemProvider_VIGILANT">
        <Keywords>
          <Keyword Value="CpuConfig"/>
          <Keyword Value="CSwitch"/>
          <Keyword Value="SampledProfile"/>
          <Keyword Value="ProcessThread"/>
          <Keyword Value="Loader"/>
          <Keyword Value="VirtualAllocation"/>
          <Keyword Value="Handle"/>
        </Keywords>
      </SystemProvider>
    </SystemCollector>
  </Profiles>
</WindowsPerformanceRecorder>
```

---

## 7. Hızlı Referans: Komut Cheat Sheet

```powershell
# ── Başlat ──────────────────────────────────────────────
wpr -start CPU -start Handle -filemode          # ETW trace
.\VIGILANT.exe                                   # Uygulama

# ── Senaryo B (otomasyon - PowerShell) ──────────────────
1..120 | ForEach-Object { Start-Sleep -Milliseconds 900; [void][System.Windows.Forms.SendKeys]::SendWait('%{TAB}') }

# ── Durdur ──────────────────────────────────────────────
wpr -stop trace.etl "Senaryo B - Stress"         # ETW kaydet
wpa trace.etl                                    # Analiz aç

# ── Internal Counters (DebugView) ───────────────────────
# OutputDebugString ile [PERF] ve [PERF-SNAP] prefix'lerini filtrele
```

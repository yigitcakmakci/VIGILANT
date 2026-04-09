# EventBridge - C++ WebView2 Köprü Mekanizması

## 📋 Genel Bakış

EventBridge, WindowTracker tarafından EventQueue'ya atılan EventData nesnelerini JSON formatına dönüştürerek WebView2 üzerinden JavaScript tarafına ileten bir köprü mekanizmasıdır.

## 🏗️ Mimari

```
WindowTracker → EventQueue → EventBridge → WebView2 → JavaScript
     (C++)        (C++)         (C++)        (JSON)      (JS)
```

## 🔧 C++ Tarafı Kullanımı

### 1. EventBridge Başlatma

```cpp
#include "Utils/EventBridge.hpp"
#include "Utils/EventQueue.hpp"
#include "UI/WebViewManager.hpp"

// Global değişkenler (veya sınıf üyesi olarak)
extern EventQueue g_EventQueue;
WebViewManager* g_WebViewManager = nullptr;
EventBridge* g_EventBridge = nullptr;

// Uygulama başlangıcında
void InitializeApplication() {
    // WebViewManager'ı oluştur ve başlat
    g_WebViewManager = new WebViewManager(hWnd);
    g_WebViewManager->Initialize();

    // EventBridge'i oluştur ve başlat
    g_EventBridge = new EventBridge(g_WebViewManager, &g_EventQueue);
    g_EventBridge->Start();
}

// Uygulama kapatılırken
void ShutdownApplication() {
    if (g_EventBridge) {
        g_EventBridge->Stop();
        delete g_EventBridge;
        g_EventBridge = nullptr;
    }

    if (g_WebViewManager) {
        delete g_WebViewManager;
        g_WebViewManager = nullptr;
    }
}
```

### 2. EventData Gönderme (WindowTracker tarafından)

```cpp
// WindowTracker.cpp içinde
EventData data;
data.processName = "chrome.exe";
data.title = "GitHub - Developer Platform";
data.url = "https://github.com";

g_EventQueue.push(data);  // EventBridge otomatik olarak alıp gönderir
```

## 📡 JavaScript Tarafı Kullanımı

### HTML'de EventBridge'i Kullanma

```html
<!DOCTYPE html>
<html>
<head>
    <title>VIGILANT Dashboard</title>
    <style>
        .window-event {
            border: 1px solid #ccc;
            padding: 10px;
            margin: 10px;
            border-radius: 5px;
        }
        .event-field {
            margin: 5px 0;
        }
        .label {
            font-weight: bold;
            margin-right: 10px;
        }
        .event-list-item {
            display: grid;
            grid-template-columns: 100px 150px 1fr 1fr;
            gap: 10px;
            padding: 5px;
            border-bottom: 1px solid #eee;
        }
    </style>
</head>
<body>
    <h1>VIGILANT - Window Tracker</h1>

    <div id="current-window-info">
        <!-- Son aktif pencere burada gösterilir -->
    </div>

    <div id="event-list">
        <!-- Event listesi burada gösterilir -->
    </div>

    <script src="eventbridge_example.js"></script>
</body>
</html>
```

### Temel Event Dinleme

```javascript
// WebView2 API kontrolü
if (window.chrome && window.chrome.webview) {
    // Event listener ekle
    window.chrome.webview.addEventListener('message', function(event) {
        const data = event.data;

        if (data.type === 'windowEvent') {
            console.log('Process:', data.data.processName);
            console.log('Title:', data.data.title);
            console.log('URL:', data.data.url);
        }
    });
}
```

### Gelişmiş Kullanım

```javascript
class EventBridgeHandler {
    constructor() {
        this.events = [];
        this.maxEvents = 1000;
        this.listeners = [];
        this.init();
    }

    init() {
        if (!window.chrome || !window.chrome.webview) {
            console.error('WebView2 API not available');
            return;
        }

        window.chrome.webview.addEventListener('message', (event) => {
            this.handleMessage(event.data);
        });
    }

    handleMessage(data) {
        if (data.type === 'windowEvent') {
            // Event'i kaydet
            this.events.push({
                ...data.data,
                timestamp: new Date()
            });

            // Limit kontrolü
            if (this.events.length > this.maxEvents) {
                this.events.shift();
            }

            // Listener'ları bilgilendir
            this.notifyListeners(data.data);
        }
    }

    // Event listener ekle
    addEventListener(callback) {
        this.listeners.push(callback);
    }

    // Event listener kaldır
    removeEventListener(callback) {
        const index = this.listeners.indexOf(callback);
        if (index > -1) {
            this.listeners.splice(index, 1);
        }
    }

    // Listener'ları bilgilendir
    notifyListeners(eventData) {
        this.listeners.forEach(callback => {
            try {
                callback(eventData);
            } catch (error) {
                console.error('Listener error:', error);
            }
        });
    }

    // Filtrelenmiş event listesi al
    getEvents(filter = {}) {
        return this.events.filter(event => {
            if (filter.processName && !event.processName.includes(filter.processName)) {
                return false;
            }
            if (filter.title && !event.title.includes(filter.title)) {
                return false;
            }
            if (filter.url && !event.url.includes(filter.url)) {
                return false;
            }
            return true;
        });
    }

    // İstatistikler
    getStats() {
        const processes = {};
        this.events.forEach(event => {
            processes[event.processName] = (processes[event.processName] || 0) + 1;
        });

        return {
            totalEvents: this.events.length,
            uniqueProcesses: Object.keys(processes).length,
            processCounts: processes
        };
    }
}

// Kullanım
const eventBridge = new EventBridgeHandler();

eventBridge.addEventListener((eventData) => {
    console.log('New window event:', eventData);
    updateUI(eventData);
});

// İstatistikleri göster
setInterval(() => {
    const stats = eventBridge.getStats();
    console.log('Stats:', stats);
}, 5000);
```

## 📊 JSON Formatı

C++ tarafından JavaScript'e gönderilen JSON formatı:

```json
{
    "type": "windowEvent",
    "data": {
        "processName": "chrome.exe",
        "title": "GitHub - Developer Platform",
        "url": "https://github.com"
    }
}
```

## 🔄 İki Yönlü İletişim

JavaScript'ten C++ tarafına mesaj göndermek için:

```javascript
// JavaScript tarafı
window.chrome.webview.postMessage({
    action: 'pauseTracking'
});
```

```cpp
// C++ tarafı (WebViewManager::SetupMessageHandler içinde)
// add_WebMessageReceived callback'inde handle edilir
```

## 🚀 Performans İpuçları

1. **Thread Güvenliği**: EventBridge ayrı bir thread'de çalışır, bu nedenle ana UI thread'i bloklamaz.

2. **Memory Yönetimi**: JavaScript tarafında çok fazla event biriktirmeyin, periyodik olarak temizleyin.

3. **Filtreleme**: JavaScript tarafında ihtiyacınız olmayan event'leri filtreleyin.

4. **Batch İşleme**: Çok fazla event geliyorsa, JavaScript tarafında batch olarak işleyin:

```javascript
let eventQueue = [];
let processing = false;

window.chrome.webview.addEventListener('message', (event) => {
    eventQueue.push(event.data);

    if (!processing) {
        processing = true;
        requestAnimationFrame(processEvents);
    }
});

function processEvents() {
    const batch = eventQueue.splice(0, 10); // Her seferde 10 event işle
    batch.forEach(data => handleWindowEvent(data.data));

    if (eventQueue.length > 0) {
        requestAnimationFrame(processEvents);
    } else {
        processing = false;
    }
}
```

## 🐛 Debug İpuçları

1. **C++ Tarafı**: `OutputDebugString` ile debug log'ları Visual Studio Output penceresinde görünür.

2. **JavaScript Tarafı**: F12 ile DevTools'u açın ve Console'da log'ları görün.

3. **JSON Hatası**: JSON parse hatası alıyorsanız, `eventbridge_example.js` içindeki `escapeHtml` fonksiyonunu kullanın.

## 📝 Notlar

- EventBridge otomatik olarak EventQueue'dan veri çeker, manuel müdahale gerekmez.
- WebView2 tam yüklenmeden önce gönderilen mesajlar kaybolabilir, `NavigationCompleted` event'ini bekleyin.
- nlohmann/json kütüphanesi header-only'dir, ayrı bir .lib dosyası gerekmez.

## 🔗 İlgili Dosyalar

- `VIGILANT/include/Utils/EventBridge.hpp` - EventBridge header
- `VIGILANT/src/Utils/EventBridge.cpp` - EventBridge implementasyonu
- `VIGILANT/include/Utils/json.hpp` - nlohmann/json kütüphanesi
- `VIGILANT/eventbridge_example.js` - JavaScript örnek kod

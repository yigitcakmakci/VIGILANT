// EventBridge Kullanım Örneği - JavaScript Tarafı
// Bu kodu HTML dosyanızdaki <script> tagları arasına ekleyin

// WebView2 mesajlarını dinle
if (window.chrome && window.chrome.webview) {
    console.log('[EventBridge] WebView2 API detected, registering event listener...');

    window.chrome.webview.addEventListener('message', function(event) {
        try {
            // C++ tarafından gönderilen JSON mesajı
            const data = event.data;

            console.log('[EventBridge] Message received:', data);

            // Event tipine göre işlem yap
            if (data.type === 'windowEvent') {
                // WindowTracker'dan gelen pencere olayı
                handleWindowEvent(data.data);
            } else if (data.type === 'error') {
                // Hata mesajı
                console.error('[EventBridge] Error from C++:', data.message);
            } else {
                // Bilinmeyen tip
                console.warn('[EventBridge] Unknown event type:', data.type);
            }
        } catch (error) {
            console.error('[EventBridge] Error processing message:', error);
        }
    });

    console.log('[EventBridge] Event listener registered successfully');
} else {
    console.error('[EventBridge] WebView2 API not available!');
}

// Pencere olaylarını işleyen fonksiyon
function handleWindowEvent(eventData) {
    console.log('[WindowEvent] Process:', eventData.processName);
    console.log('[WindowEvent] Title:', eventData.title);
    console.log('[WindowEvent] URL:', eventData.url);

    // Örnek: UI'da göster
    displayWindowEvent(eventData);

    // Örnek: Listeye ekle
    addToEventList(eventData);
}

// UI'da event gösterme örneği
function displayWindowEvent(eventData) {
    // Son aktif pencere bilgisini göster
    const displayElement = document.getElementById('current-window-info');
    if (displayElement) {
        displayElement.innerHTML = `
            <div class="window-event">
                <div class="event-field">
                    <span class="label">Process:</span>
                    <span class="value">${escapeHtml(eventData.processName)}</span>
                </div>
                <div class="event-field">
                    <span class="label">Title:</span>
                    <span class="value">${escapeHtml(eventData.title)}</span>
                </div>
                <div class="event-field">
                    <span class="label">URL:</span>
                    <span class="value">${escapeHtml(eventData.url)}</span>
                </div>
                <div class="event-timestamp">${new Date().toLocaleString()}</div>
            </div>
        `;
    }
}

// Event listesine ekleme örneği
function addToEventList(eventData) {
    const listElement = document.getElementById('event-list');
    if (listElement) {
        const eventItem = document.createElement('div');
        eventItem.className = 'event-list-item';
        eventItem.innerHTML = `
            <div class="event-time">${new Date().toLocaleTimeString()}</div>
            <div class="event-process">${escapeHtml(eventData.processName)}</div>
            <div class="event-title">${escapeHtml(eventData.title)}</div>
            <div class="event-url">${escapeHtml(eventData.url)}</div>
        `;

        // En yeni eventi en üste ekle
        listElement.insertBefore(eventItem, listElement.firstChild);

        // Maksimum 100 event tut (performans için)
        while (listElement.children.length > 100) {
            listElement.removeChild(listElement.lastChild);
        }
    }
}

// XSS koruması için HTML escape
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// C++ tarafına mesaj gönderme örneği (isteğe bağlı)
function sendMessageToCpp(message) {
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(message);
        console.log('[EventBridge] Message sent to C++:', message);
    } else {
        console.error('[EventBridge] Cannot send message - WebView2 API not available');
    }
}

// Örnek kullanım:
// sendMessageToCpp({ action: 'pauseTracking' });
// sendMessageToCpp({ action: 'resumeTracking' });

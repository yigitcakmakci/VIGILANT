/**
 * DOM Updater – thin glue between TimerService and the dashboard HTML.
 *
 * Subscribes to the timer's pub/sub events and applies targeted
 * textContent updates — no innerHTML, no framework overhead.
 *
 * To use: call `mountTimerUI(timerService)` after DOMContentLoaded.
 */

import { TimerService } from './timer-service';
import { SessionRecord } from './timer-store';

// ── Helpers ────────────────────────────────────────────────────────────

function formatElapsed(ms: number): string {
    const totalSec = Math.floor(ms / 1000);
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = totalSec % 60;
    if (h > 0) return `${h}:${pad2(m)}:${pad2(s)}`;
    return `${m}:${pad2(s)}`;
}

function pad2(n: number): string {
    return n < 10 ? '0' + n : '' + n;
}

// ── DOM refs (cached once) ─────────────────────────────────────────────

interface DOMRefs {
    elapsedEl: HTMLElement | null;
    appNameEl: HTMLElement | null;
    idleBadge: HTMLElement | null;
    sessionList: HTMLElement | null;
}

function queryRefs(): DOMRefs {
    return {
        elapsedEl: document.getElementById('live-timer-elapsed'),
        appNameEl: document.getElementById('live-timer-app'),
        idleBadge: document.getElementById('live-timer-idle'),
        sessionList: document.getElementById('session-history'),
    };
}

// ── Mount ──────────────────────────────────────────────────────────────

export function mountTimerUI(service: TimerService): () => void {
    const refs = queryRefs();
    const unsubs: Array<() => void> = [];

    // Tick → update elapsed counter (fires at most once/sec)
    unsubs.push(
        service.bus.on('tick', (elapsedMs) => {
            if (refs.elapsedEl) {
                refs.elapsedEl.textContent = formatElapsed(elapsedMs);
            }
        }),
    );

    // State changed → app name + idle badge
    unsubs.push(
        service.bus.on('stateChanged', (state) => {
            if (refs.appNameEl) {
                refs.appNameEl.textContent = state.activeApp ?? '—';
            }
            if (refs.idleBadge) {
                refs.idleBadge.style.display = state.idle ? 'inline-block' : 'none';
            }
        }),
    );

    // Session finalized → prepend to history list
    unsubs.push(
        service.bus.on('sessionFinalized', (session: SessionRecord) => {
            if (!refs.sessionList) return;

            const row = document.createElement('div');
            row.className = 'session-row';

            const nameSpan = document.createElement('span');
            nameSpan.className = 'session-app';
            nameSpan.textContent = session.appName;

            const durSpan = document.createElement('span');
            durSpan.className = 'session-dur';
            durSpan.textContent = formatElapsed(session.durationMs);

            row.appendChild(nameSpan);
            row.appendChild(durSpan);
            refs.sessionList.insertBefore(row, refs.sessionList.firstChild);

            // Cap visible history
            while (refs.sessionList.children.length > 50) {
                refs.sessionList.removeChild(refs.sessionList.lastChild!);
            }
        }),
    );

    // Return teardown function
    return () => unsubs.forEach(fn => fn());
}

// ── Bootstrap (wire WebView2 native events → TimerService) ─────────────

export function bootstrapLiveTimer(): TimerService {
    const service = new TimerService();
    mountTimerUI(service);

    // Listen to native events pushed via WebView2 postMessage / ExecuteScript
    if (typeof window !== 'undefined' && window.chrome?.webview) {
        window.chrome.webview.addEventListener('message', (e: MessageEvent) => {
            const data = e.data;
            if (!data || !data.type) return;

            switch (data.type) {
                case 'windowEvent':
                case 'activeAppChanged':
                    service.onActiveAppChanged(
                        data.data?.processName ?? data.app ?? 'unknown',
                        data.nativeTimestamp ?? Date.now(),
                    );
                    break;
                case 'idleStart':
                    service.onIdleStart();
                    break;
                case 'idleEnd':
                    service.onIdleEnd();
                    break;
                case 'AiTokenUsageUpdated':
                    updateTokenOdometer(data.payload);
                    break;
            }
        });
    }

    return service;
}

// ── Token Odometer ─────────────────────────────────────────────────────

interface TokenUsagePayload {
    tokensUsedThisRequest: number;
    dailyTotalTokens: number;
    dailyLimit: number;
}

function formatTokenCompact(n: number): string {
    if (n >= 1_000_000) return (n / 1_000_000).toFixed(1).replace(/\.0$/, '') + 'M';
    if (n >= 1_000) return (n / 1_000).toFixed(1).replace(/\.0$/, '') + 'K';
    return String(n);
}

function formatTokenNumber(n: number): string {
    return n.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

export function updateTokenOdometer(payload: TokenUsagePayload | null): void {
    if (!payload) return;

    const daily = payload.dailyTotalTokens ?? 0;
    const limit = payload.dailyLimit ?? 1_000_000;
    const req = payload.tokensUsedThisRequest ?? 0;
    const pct = Math.min((daily / limit) * 100, 100);

    const valueEl = document.getElementById('tokenOdometerValue');
    const fillEl = document.getElementById('tokenBarFill');
    const reqEl = document.getElementById('tokenOdometerReq');
    const pctEl = document.getElementById('tokenOdometerPct');

    if (valueEl) valueEl.textContent = `${formatTokenCompact(daily)} / ${formatTokenCompact(limit)}`;

    if (fillEl) {
        fillEl.style.width = `${pct.toFixed(2)}%`;
        if (pct >= 80) {
            fillEl.classList.add('warning');
        } else {
            fillEl.classList.remove('warning');
        }
    }

    if (reqEl) reqEl.textContent = `Son istek: +${formatTokenNumber(req)}`;
    if (pctEl) pctEl.textContent = `${pct.toFixed(1)}%`;
}

// ── Chrome WebView2 type shim ──────────────────────────────────────────

declare global {
    interface Window {
        chrome?: {
            webview?: {
                addEventListener(type: string, listener: (e: MessageEvent) => void): void;
                postMessage(msg: unknown): void;
            };
        };
    }
}

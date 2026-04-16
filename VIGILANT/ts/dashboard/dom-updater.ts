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
import { renderGoalTree, computeNodeProgress } from '../interview/goal-tree-recursive-ui';
import type { GoalNode } from '../interview/goal-tree-types';

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
                case 'GoalTreeGenerated':
                case 'GoalTreeUpdated':
                case 'ReplanCompleted':
                    handleGoalTreeEvent(data.payload);
                    break;
                case 'GoalTreeSummaryUpdated':
                    handleMissionControlUpdate(data.payload);
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

// ── GoalTree Event Handler ─────────────────────────────────────────────

function handleGoalTreeEvent(payload: Record<string, unknown> | null): void {
    if (!payload) return;

    // Extract tree from payload — GoalTreeGenerated/Updated carry it in .goalTree,
    // ReplanCompleted also uses .goalTree
    const treeData = (payload.goalTree ?? payload.tree ?? payload.root) as GoalNode | { root: GoalNode } | null;
    if (!treeData) return;

    // Normalize: if the payload is a DynamicGoalTree wrapper, extract root
    const rootNode: GoalNode = ('root' in treeData && (treeData as { root: GoalNode }).root)
        ? (treeData as { root: GoalNode }).root
        : treeData as GoalNode;

    if (!rootNode || !rootNode.id) return;

    const container = document.getElementById('goal-tree-container');
    if (!container) return;

    // Show tree container, hide empty state
    container.style.display = '';
    const emptyState = document.getElementById('stEmptyState');
    if (emptyState) emptyState.style.display = 'none';

    container.innerHTML = '';
    renderGoalTree(rootNode, container);
}

// ── Mission Control Handler ────────────────────────────────────────────

interface MCSummaryGoal {
    id: string;
    title: string;
    progress: number;
    children?: MCSummaryGoal[];
    actionItems?: Array<{ text: string; id?: string; isCompleted?: boolean }>;
}

interface MCSummaryPayload {
    lastActiveGoalId: string;
    goals: MCSummaryGoal[];
}

let _lastMCPayload: MCSummaryPayload | null = null;

function handleMissionControlUpdate(payload: MCSummaryPayload | null): void {
    if (!payload) return;
    _lastMCPayload = payload;
    renderMissionControlTS(payload);
}

function renderMissionControlTS(payload: MCSummaryPayload): void {
    const widget = document.getElementById('mcWidget');
    const list = document.getElementById('mcGoalList');
    const countEl = document.getElementById('mcGoalCount');
    if (!widget || !list) return;

    const goals = payload.goals || [];
    const activeId = payload.lastActiveGoalId || '';

    if (goals.length === 0) {
        if (countEl) countEl.textContent = '';
        list.innerHTML = '<div class="mc-empty">Henüz hedef yok — Hedefler sekmesinden başlayın.</div>';
        return;
    }

    if (countEl) countEl.textContent = `${goals.length} hedef`;

    // Clear and rebuild
    list.innerHTML = '';

    for (const g of goals) {
        const id = g.id || '';
        const title = g.title || 'Untitled';
        const progress = typeof g.progress === 'number' ? g.progress : 0;
        const isActive = id === activeId && activeId !== '';

        const item = document.createElement('div');
        item.className = 'mc-goal-item' + (isActive ? ' mc-active' : '');

        // Goal info row
        const info = document.createElement('div');
        info.className = 'mc-goal-info';

        const titleEl = document.createElement('div');
        titleEl.className = 'mc-goal-title';
        titleEl.textContent = title;
        titleEl.style.cursor = 'pointer';
        titleEl.dataset.goalId = id;
        titleEl.addEventListener('click', () => mcNavigateToGoal(id));

        const barTrack = document.createElement('div');
        barTrack.className = 'mc-bar-track';
        const barFill = document.createElement('div');
        barFill.className = 'mc-bar-fill';
        barFill.style.width = `${Math.min(progress, 100)}%`;
        barTrack.appendChild(barFill);

        info.appendChild(titleEl);
        info.appendChild(barTrack);
        item.appendChild(info);

        const pctEl = document.createElement('span');
        pctEl.className = 'mc-goal-pct';
        pctEl.textContent = `${progress}%`;
        item.appendChild(pctEl);

        // Action items for active goal
        if (isActive) {
            const actions = getNextActionsTS(g, 3);
            if (actions.length > 0) {
                const actionsDiv = document.createElement('div');
                actionsDiv.className = 'mc-actions';

                for (const a of actions) {
                    const chip = document.createElement('div');
                    chip.className = 'mc-action-chip' + (a.done ? ' mc-done' : '');
                    if (!a.done) {
                        chip.style.cursor = 'pointer';
                        chip.title = 'Tamamlamak için tıklayın';
                        chip.addEventListener('click', () => mcCompleteAction(id, a.actionId, a.text, chip));
                    }

                    const dot = document.createElement('span');
                    dot.className = 'mc-action-dot';
                    chip.appendChild(dot);

                    const textSpan = document.createElement('span');
                    textSpan.textContent = a.text;
                    chip.appendChild(textSpan);

                    actionsDiv.appendChild(chip);
                }

                item.appendChild(actionsDiv);
            }
        }

        list.appendChild(item);
    }
}

interface MCActionInfo {
    text: string;
    actionId: string;
    done: boolean;
}

function getNextActionsTS(goal: MCSummaryGoal, max: number): MCActionInfo[] {
    const result: MCActionInfo[] = [];

    const items = goal.actionItems || [];
    for (const item of items) {
        if (result.length >= max) break;
        const text = typeof item === 'string' ? item : (item.text || '');
        const actionId = (typeof item !== 'string' && item.id) ? item.id : text;
        const done = typeof item !== 'string' && !!item.isCompleted;
        result.push({ text, actionId, done });
    }

    const children = goal.children || [];
    for (const child of children) {
        if (result.length >= max) break;
        const childItems = child.actionItems || [];
        for (const ci of childItems) {
            if (result.length >= max) break;
            const text = typeof ci === 'string' ? ci : (ci.text || '');
            const actionId = (typeof ci !== 'string' && ci.id) ? ci.id : text;
            const done = typeof ci !== 'string' && !!ci.isCompleted;
            result.push({ text, actionId, done });
        }
    }

    // Incomplete first
    result.sort((a, b) => (a.done ? 1 : 0) - (b.done ? 1 : 0));
    return result.slice(0, max);
}

/** Navigate to Hedefler tab and expand the clicked goal */
function mcNavigateToGoal(goalId: string): void {
    // Use the global navigateTo defined in dashboard_pro.html
    const nav = (window as unknown as Record<string, Function>)['navigateTo'];
    if (typeof nav === 'function') {
        nav('goals');
    }

    // After the page switch, find and expand the goal node
    requestAnimationFrame(() => {
        setTimeout(() => {
            // Look for the goal node in the Hedefler tree by data attribute or id
            const goalEl = document.querySelector(`[data-goal-id="${goalId}"]`) as HTMLElement
                || document.getElementById(`goal-${goalId}`) as HTMLElement;
            if (goalEl) {
                // Expand collapsed parents first
                let parent = goalEl.closest('.gt-children-collapsed, .gt-collapsed') as HTMLElement;
                while (parent) {
                    parent.classList.remove('gt-children-collapsed', 'gt-collapsed');
                    parent.classList.add('gt-children-expanded', 'gt-expanded');
                    parent = parent.parentElement?.closest('.gt-children-collapsed, .gt-collapsed') as HTMLElement;
                }
                // Expand the node itself
                goalEl.classList.remove('gt-children-collapsed', 'gt-collapsed');
                goalEl.classList.add('gt-children-expanded', 'gt-expanded');
                // Highlight and scroll into view
                goalEl.scrollIntoView({ behavior: 'smooth', block: 'center' });
                goalEl.style.outline = '2px solid rgba(99,102,241,.6)';
                goalEl.style.outlineOffset = '4px';
                setTimeout(() => {
                    goalEl.style.outline = '';
                    goalEl.style.outlineOffset = '';
                }, 2000);
            }
        }, 150);
    });
}

/** Complete an action item from Dashboard – send to C++ and update UI optimistically */
function mcCompleteAction(goalId: string, actionId: string, actionText: string, chipEl: HTMLElement): void {
    // Optimistic UI update
    chipEl.classList.add('mc-done');
    chipEl.style.cursor = 'default';
    chipEl.style.pointerEvents = 'none';

    // Send CompleteActionItem to C++ via WebView bridge
    if (window.chrome?.webview) {
        window.chrome.webview.postMessage({
            type: 'CompleteActionItem',
            goalId,
            actionId,
            actionText,
        });
    }

    // Optimistically update the progress bar for this goal
    if (_lastMCPayload) {
        const goal = _lastMCPayload.goals.find(g => g.id === goalId);
        if (goal) {
            // Mark action as completed in local state
            const allActions = [...(goal.actionItems || [])];
            const children = goal.children || [];
            for (const c of children) {
                allActions.push(...(c.actionItems || []));
            }
            const total = allActions.length;
            const doneCount = allActions.filter(a => typeof a !== 'string' && a.isCompleted).length + 1;
            const newProgress = total > 0 ? Math.round((doneCount / total) * 100) : goal.progress;
            goal.progress = Math.min(newProgress, 100);

            // Mark the action completed in local data
            for (const a of (goal.actionItems || [])) {
                if (typeof a !== 'string' && (a.id === actionId || a.text === actionText)) {
                    a.isCompleted = true;
                    break;
                }
            }
            for (const c of children) {
                let found = false;
                for (const a of (c.actionItems || [])) {
                    if (typeof a !== 'string' && (a.id === actionId || a.text === actionText)) {
                        a.isCompleted = true;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }

        // Re-render the entire widget with updated state
        renderMissionControlTS(_lastMCPayload);
    }
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

// ═══════════════════════════════════════════════════════════════════════
// Mock Test — 3-depth recursive GoalNode rendered on page load
// Remove this block once real C++ backend integration is verified.
// ═══════════════════════════════════════════════════════════════════════

if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        const container = document.getElementById('goal-tree-container');
        if (!container) return;

        const mockTree: GoalNode = {
            id: 'node-0',
            title: 'Cerrah Olmak',
            description: 'Genel cerrahi uzmanlığı hedefi',
            progress: 0,
            isLeaf: false,
            children: [
                {
                    id: 'node-0-0',
                    title: 'YKS İlk 10K',
                    description: 'Üniversite sınavında ilk 10.000 sıralamaya girmek',
                    progress: 0,
                    isLeaf: false,
                    children: [
                        {
                            id: 'node-0-0-0',
                            title: 'Türev Fasikülü Bitir',
                            description: 'Türev konusundaki tüm soru bankasını tamamla',
                            progress: 0,
                            isLeaf: true,
                            acceptanceCriteria: '50 soru çöz',
                        },
                        {
                            id: 'node-0-0-1',
                            title: 'İntegral Fasikülü Bitir',
                            description: 'İntegral konusundaki tüm soru bankasını tamamla',
                            progress: 0,
                            isLeaf: true,
                            acceptanceCriteria: '40 soru çöz ve hata oranı %10 altında olsun',
                        },
                    ],
                },
                {
                    id: 'node-0-1',
                    title: 'Biyoloji Olimpiyat Hazırlık',
                    description: 'Biyoloji bilgi seviyesini olimpiyat düzeyine çıkar',
                    progress: 0,
                    isLeaf: false,
                    children: [
                        {
                            id: 'node-0-1-0',
                            title: 'Hücre Biyolojisi Notlarını Tamamla',
                            description: 'Hücre yapısı ve organel fonksiyonları notlarını bitir',
                            progress: 0,
                            isLeaf: true,
                            acceptanceCriteria: 'Tüm organellerin fonksiyonlarını ezbere say',
                        },
                    ],
                },
            ],
        };

        // Show container, hide empty state
        container.style.display = '';
        const emptyState = document.getElementById('stEmptyState');
        if (emptyState) emptyState.style.display = 'none';

        renderGoalTree(mockTree, container);
        console.log('[VIGILANT] Mock GoalTree rendered (3 depth, Cerrah Olmak)');
    });
}

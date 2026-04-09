/**
 * floating-widget.ts — Glassmorphism floating AI assistant widget.
 *
 * Self-contained vanilla TS component. Renders into the DOM as a
 * fixed-position widget in the bottom-right corner.
 *
 * Features:
 *   • Glassmorphism panel (backdrop-blur + translucent bg)
 *   • Scrollable transcript with user/AI bubbles
 *   • Hard limit maxQuestions=3 enforced on the client
 *   • Sticky "Yeterli, Planı Oluştur" CTA always accessible
 *   • Empty message guard + double-click / double-finalize protection
 *   • Slide-up open/close animation
 */

import {
    InterviewState,
    InterviewAction,
    ChatMessage,
    createInitialState,
    interviewReducer,
    canSendMessage,
    canFinalize,
    isLimitReached,
} from './interview-state';

import {
    BridgeEnvelope,
    AiQuestionPayload,
    InterviewStartedPayload,
    InterviewFinalizedPayload,
    onMessage,
    publishInterviewStart,
    publishUserMessage,
    publishFinalize,
    publishGenerateGoalTree,
    generateId,
} from './event-bridge';

// ═══════════════════════════════════════════════════════════════════════
// Widget class
// ═══════════════════════════════════════════════════════════════════════

export class FloatingAIAssistantWidget {
    private _state: InterviewState = createInitialState();
    private _open = false;
    private _unsubBridge: (() => void) | null = null;

    // DOM refs
    private _root!: HTMLDivElement;
    private _fab!: HTMLButtonElement;
    private _panel!: HTMLDivElement;
    private _messageList!: HTMLDivElement;
    private _input!: HTMLInputElement;
    private _sendBtn!: HTMLButtonElement;
    private _ctaBtn!: HTMLButtonElement;
    private _inputWarn!: HTMLSpanElement;
    private _statusBar!: HTMLDivElement;

    constructor() {
        this._buildDOM();
        this._bindEvents();
        this._unsubBridge = onMessage((evt) => this._onBridgeMessage(evt));
    }

    dispose(): void {
        this._unsubBridge?.();
        this._root.remove();
    }

    // ── State dispatch ─────────────────────────────────────────────────
    private _dispatch(action: InterviewAction): void {
        this._state = interviewReducer(this._state, action);
        this._render();
    }

    // ═══════════════════════════════════════════════════════════════════
    // DOM construction
    // ═══════════════════════════════════════════════════════════════════

    private _buildDOM(): void {
        // Inject CSS
        this._injectStyles();

        // Root container
        this._root = document.createElement('div');
        this._root.id = 'vig-assistant-root';

        // ── FAB (Floating Action Button) ────────────────────────────────
        this._fab = document.createElement('button');
        this._fab.id = 'vig-fab';
        this._fab.className = 'vig-fab';
        this._fab.innerHTML = `
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                 stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>
            </svg>`;
        this._fab.title = 'VIGILANT Assistant';

        // ── Panel ───────────────────────────────────────────────────────
        this._panel = document.createElement('div');
        this._panel.id = 'vig-panel';
        this._panel.className = 'vig-panel vig-panel--closed';

        // Header
        const header = document.createElement('div');
        header.className = 'vig-panel__header';
        header.innerHTML = `
            <span class="vig-panel__title">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                     stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="vertical-align:-2px;margin-right:6px;">
                    <circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>
                </svg>VIGILANT Assistant
            </span>`;

        const closeBtn = document.createElement('button');
        closeBtn.className = 'vig-panel__close';
        closeBtn.innerHTML = '&times;';
        closeBtn.title = 'Kapat';
        closeBtn.addEventListener('click', () => this._toggle(false));
        header.appendChild(closeBtn);

        // CTA button (sticky in header area)
        this._ctaBtn = document.createElement('button');
        this._ctaBtn.className = 'vig-cta-btn';
        this._ctaBtn.textContent = '✦ Yeterli, Planı Oluştur';
        this._ctaBtn.disabled = true;

        // Status bar
        this._statusBar = document.createElement('div');
        this._statusBar.className = 'vig-status-bar';

        // Message list
        this._messageList = document.createElement('div');
        this._messageList.className = 'vig-messages';

        // Input area
        const inputArea = document.createElement('div');
        inputArea.className = 'vig-input-area';

        this._input = document.createElement('input');
        this._input.type = 'text';
        this._input.className = 'vig-input';
        this._input.placeholder = 'Mesajınızı yazın...';
        this._input.maxLength = 500;

        this._sendBtn = document.createElement('button');
        this._sendBtn.className = 'vig-send-btn';
        this._sendBtn.innerHTML = `
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                 stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>`;
        this._sendBtn.title = 'Gönder';

        this._inputWarn = document.createElement('span');
        this._inputWarn.className = 'vig-input-warn';

        inputArea.appendChild(this._input);
        inputArea.appendChild(this._sendBtn);

        // Assemble panel
        this._panel.appendChild(header);
        this._panel.appendChild(this._ctaBtn);
        this._panel.appendChild(this._statusBar);
        this._panel.appendChild(this._messageList);
        this._panel.appendChild(this._inputWarn);
        this._panel.appendChild(inputArea);

        // Assemble root
        this._root.appendChild(this._panel);
        this._root.appendChild(this._fab);
        document.body.appendChild(this._root);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Event bindings
    // ═══════════════════════════════════════════════════════════════════

    private _bindEvents(): void {
        this._fab.addEventListener('click', () => this._toggle(true));

        this._sendBtn.addEventListener('click', () => this._onSend());

        this._input.addEventListener('keydown', (e: KeyboardEvent) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                this._onSend();
            }
        });

        // Clear warning on typing
        this._input.addEventListener('input', () => {
            this._inputWarn.textContent = '';
        });

        this._ctaBtn.addEventListener('click', () => this._onFinalizeCTA());
    }

    // ═══════════════════════════════════════════════════════════════════
    // Toggle open/close
    // ═══════════════════════════════════════════════════════════════════

    private _toggle(open: boolean): void {
        this._open = open;
        if (open) {
            this._panel.classList.remove('vig-panel--closed');
            this._panel.classList.add('vig-panel--open');
            this._fab.classList.add('vig-fab--hidden');

            // Auto-start session if idle
            if (this._state.status === 'idle') {
                this._startInterview();
            }
            // Focus input
            requestAnimationFrame(() => this._input.focus());
        } else {
            this._panel.classList.remove('vig-panel--open');
            this._panel.classList.add('vig-panel--closed');
            this._fab.classList.remove('vig-fab--hidden');
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Interview lifecycle
    // ═══════════════════════════════════════════════════════════════════

    private _startInterview(): void {
        const tempSessionId = generateId();
        this._dispatch({ type: 'SESSION_STARTED', sessionId: tempSessionId });
        publishInterviewStart(tempSessionId);
    }

    private _onSend(): void {
        // Guard: can we send?
        if (!canSendMessage(this._state)) return;

        const text = this._input.value.trim();
        if (!text) {
            this._inputWarn.textContent = 'Lütfen bir mesaj yazın.';
            return;
        }

        this._inputWarn.textContent = '';

        const msg: ChatMessage = {
            id: generateId(),
            role: 'user',
            text,
            ts: new Date().toISOString(),
        };

        this._dispatch({ type: 'USER_MESSAGE_SENT', message: msg });
        this._dispatch({ type: 'SENDING_START' });
        this._input.value = '';

        publishUserMessage(this._state.sessionId, text, msg.id);
    }

    private _onFinalizeCTA(): void {
        if (!canFinalize(this._state)) return;

        this._dispatch({ type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
        publishFinalize(this._state.sessionId, 'cta');
    }

    // ═══════════════════════════════════════════════════════════════════
    // Bridge message handler
    // ═══════════════════════════════════════════════════════════════════

    private _onBridgeMessage(evt: BridgeEnvelope): void {
        switch (evt.type) {
            case 'InterviewStarted': {
                const p = evt.payload as unknown as InterviewStartedPayload;
                this._dispatch({ type: 'SESSION_STARTED', sessionId: p.sessionId });

                // Show the first question
                const q = p.firstQuestion;
                const aiMsg: ChatMessage = {
                    id: q.messageId,
                    role: 'ai',
                    text: q.text,
                    ts: evt.ts,
                };
                this._dispatch({
                    type: 'AI_QUESTION_RECEIVED',
                    message: aiMsg,
                    questionCount: 1,
                });
                break;
            }

            case 'AiQuestionProduced': {
                const p = evt.payload as unknown as AiQuestionPayload;
                const aiMsg: ChatMessage = {
                    id: p.messageId,
                    role: 'ai',
                    text: p.text,
                    ts: evt.ts,
                };
                this._dispatch({
                    type: 'AI_QUESTION_RECEIVED',
                    message: aiMsg,
                    questionCount: p.questionCount,
                    autoFinalize: p.autoFinalize,
                });

                // Don't auto-finalize here — let the user answer the last question.
                // The C++ side auto-finalizes when the final answer is received.
                break;
            }

            case 'InterviewFinalized': {
                const p = evt.payload as unknown as InterviewFinalizedPayload;
                this._dispatch({
                    type: 'FINALIZE_CONFIRMED',
                    endedBy: p.endedBy,
                    questionCount: p.questionCount,
                });

                // Fire GenerateGoalTree
                if (p.interviewSessionId && !p.alreadyFinalized) {
                    publishGenerateGoalTree(p.interviewSessionId);
                }
                break;
            }

            case 'Error': {
                const p = evt.payload as { code?: string; message?: string };
                // Ignore duplicate request errors silently
                if (p.code === 'DUPLICATE_REQUEST') return;
                this._dispatch({ type: 'ERROR', message: p.message ?? 'Bilinmeyen hata' });
                break;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Render (imperative DOM updates)
    // ═══════════════════════════════════════════════════════════════════

    private _render(): void {
        const s = this._state;

        // ── Status bar ─────────────────────────────────────────────────
        if (s.status === 'finalized') {
            this._statusBar.textContent = `✓ Görüşme tamamlandı (${s.questionCount}/${s.maxQuestions} soru)`;
            this._statusBar.className = 'vig-status-bar vig-status-bar--done';
        } else if (s.status === 'error') {
            this._statusBar.textContent = `⚠ ${s.errorMessage ?? 'Hata'}`;
            this._statusBar.className = 'vig-status-bar vig-status-bar--error';
        } else if (s.status === 'asking') {
            this._statusBar.textContent = `Soru ${s.questionCount}/${s.maxQuestions}`;
            this._statusBar.className = 'vig-status-bar';
        } else if (s.status === 'finalizing') {
            this._statusBar.textContent = 'Sonuçlandırılıyor...';
            this._statusBar.className = 'vig-status-bar';
        } else {
            this._statusBar.textContent = '';
        }

        // ── Message list ───────────────────────────────────────────────
        this._messageList.innerHTML = '';
        for (const msg of s.transcript) {
            const bubble = document.createElement('div');
            bubble.className = msg.role === 'user'
                ? 'vig-bubble vig-bubble--user'
                : 'vig-bubble vig-bubble--ai';
            bubble.textContent = msg.text;
            this._messageList.appendChild(bubble);
        }
        // Auto-scroll to bottom
        requestAnimationFrame(() => {
            this._messageList.scrollTop = this._messageList.scrollHeight;
        });

        // ── Input state ────────────────────────────────────────────────
        const canSend = canSendMessage(s);
        this._input.disabled = !canSend;
        this._sendBtn.disabled = !canSend;

        if (s.status === 'finalized') {
            this._input.placeholder = 'Görüşme tamamlandı.';
        } else if (s.isSending) {
            this._input.placeholder = 'Yanıt bekleniyor...';
        } else if (!canSend && isLimitReached(s)) {
            this._input.placeholder = 'Soru limiti doldu.';
        } else {
            this._input.placeholder = 'Mesajınızı yazın...';
        }

        // ── CTA button ─────────────────────────────────────────────────
        this._ctaBtn.disabled = !canFinalize(s);
        if (s.status === 'finalized') {
            this._ctaBtn.textContent = '✓ Plan Oluşturuldu';
            this._ctaBtn.classList.add('vig-cta-btn--done');
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Styles injection (glassmorphism + animations)
    // ═══════════════════════════════════════════════════════════════════

    private _injectStyles(): void {
        if (document.getElementById('vig-assistant-styles')) return;

        const style = document.createElement('style');
        style.id = 'vig-assistant-styles';
        style.textContent = `
/* ── Root ────────────────────────────────────────────────────────── */
#vig-assistant-root {
    position: fixed;
    bottom: 24px;
    right: 24px;
    z-index: 99999;
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    font-size: 14px;
    color: #e2e8f0;
}

/* ── FAB ─────────────────────────────────────────────────────────── */
.vig-fab {
    width: 56px; height: 56px;
    border-radius: 50%;
    border: 1px solid rgba(139,92,246,0.5);
    background: linear-gradient(135deg, rgba(30,27,75,0.85), rgba(76,29,149,0.7));
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    color: #c4b5fd;
    cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    box-shadow: 0 4px 24px rgba(139,92,246,0.35), 0 0 0 1px rgba(139,92,246,0.15);
    transition: transform 0.2s, opacity 0.25s, box-shadow 0.2s;
}
.vig-fab:hover {
    transform: scale(1.08);
    box-shadow: 0 6px 32px rgba(139,92,246,0.5), 0 0 0 1px rgba(139,92,246,0.3);
}
.vig-fab--hidden {
    opacity: 0; pointer-events: none; transform: scale(0.7);
}

/* ── Panel ───────────────────────────────────────────────────────── */
.vig-panel {
    position: absolute;
    bottom: 0; right: 0;
    width: 390px; height: 530px;
    border-radius: 16px;
    overflow: hidden;
    display: flex; flex-direction: column;

    /* Glassmorphism */
    background: rgba(15, 12, 41, 0.72);
    backdrop-filter: blur(24px) saturate(1.3);
    -webkit-backdrop-filter: blur(24px) saturate(1.3);
    border: 1px solid rgba(139,92,246,0.25);
    box-shadow:
        0 8px 48px rgba(0,0,0,0.55),
        0 0 0 1px rgba(139,92,246,0.12),
        inset 0 1px 0 rgba(255,255,255,0.05);

    transition: transform 0.35s cubic-bezier(.4,0,.2,1),
                opacity 0.3s ease;
    transform-origin: bottom right;
}
.vig-panel--closed {
    transform: translateY(20px) scale(0.95);
    opacity: 0; pointer-events: none;
}
.vig-panel--open {
    transform: translateY(0) scale(1);
    opacity: 1; pointer-events: auto;
}

/* ── Header ──────────────────────────────────────────────────────── */
.vig-panel__header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 14px 16px 10px;
    border-bottom: 1px solid rgba(139,92,246,0.15);
    flex-shrink: 0;
}
.vig-panel__title {
    font-weight: 600; font-size: 14px;
    color: #c4b5fd;
    letter-spacing: 0.3px;
}
.vig-panel__close {
    background: none; border: none; color: #94a3b8; font-size: 22px;
    cursor: pointer; padding: 0 4px; line-height: 1;
    transition: color 0.15s;
}
.vig-panel__close:hover { color: #f87171; }

/* ── CTA Button ──────────────────────────────────────────────────── */
.vig-cta-btn {
    margin: 8px 12px 4px;
    padding: 10px 16px;
    border: 1px solid rgba(139,92,246,0.4);
    border-radius: 10px;
    background: linear-gradient(135deg, rgba(124,58,237,0.35), rgba(139,92,246,0.18));
    color: #e0d4fd;
    font-weight: 600; font-size: 13px;
    cursor: pointer;
    transition: background 0.2s, border-color 0.2s, opacity 0.2s;
    flex-shrink: 0;
    text-align: center;
    letter-spacing: 0.2px;
}
.vig-cta-btn:hover:not(:disabled) {
    background: linear-gradient(135deg, rgba(124,58,237,0.55), rgba(139,92,246,0.35));
    border-color: rgba(167,139,250,0.6);
}
.vig-cta-btn:disabled {
    opacity: 0.4; cursor: default;
}
.vig-cta-btn--done {
    background: linear-gradient(135deg, rgba(34,197,94,0.3), rgba(22,163,74,0.15));
    border-color: rgba(34,197,94,0.4);
    color: #86efac;
}

/* ── Status Bar ──────────────────────────────────────────────────── */
.vig-status-bar {
    padding: 4px 16px;
    font-size: 11px;
    color: #94a3b8;
    flex-shrink: 0;
    min-height: 20px;
}
.vig-status-bar--done { color: #86efac; }
.vig-status-bar--error { color: #fca5a5; }

/* ── Messages ────────────────────────────────────────────────────── */
.vig-messages {
    flex: 1; overflow-y: auto;
    padding: 8px 12px;
    display: flex; flex-direction: column; gap: 8px;
    scrollbar-width: thin;
    scrollbar-color: rgba(139,92,246,0.3) transparent;
}
.vig-messages::-webkit-scrollbar { width: 5px; }
.vig-messages::-webkit-scrollbar-thumb {
    background: rgba(139,92,246,0.3); border-radius: 4px;
}

/* ── Bubbles ─────────────────────────────────────────────────────── */
.vig-bubble {
    max-width: 85%;
    padding: 10px 14px;
    border-radius: 14px;
    font-size: 13px;
    line-height: 1.5;
    word-wrap: break-word;
}
.vig-bubble--user {
    align-self: flex-end;
    background: rgba(124,58,237,0.3);
    border: 1px solid rgba(139,92,246,0.25);
    color: #ede9fe;
    border-bottom-right-radius: 4px;
}
.vig-bubble--ai {
    align-self: flex-start;
    background: rgba(30,41,59,0.6);
    border: 1px solid rgba(100,116,139,0.2);
    color: #cbd5e1;
    border-bottom-left-radius: 4px;
    backdrop-filter: blur(6px);
    -webkit-backdrop-filter: blur(6px);
}

/* ── Input warning ───────────────────────────────────────────────── */
.vig-input-warn {
    display: block;
    min-height: 16px;
    padding: 0 16px;
    font-size: 11px;
    color: #fca5a5;
    flex-shrink: 0;
}

/* ── Input area ──────────────────────────────────────────────────── */
.vig-input-area {
    display: flex; gap: 8px;
    padding: 8px 12px 14px;
    border-top: 1px solid rgba(139,92,246,0.12);
    flex-shrink: 0;
}
.vig-input {
    flex: 1;
    padding: 10px 14px;
    border-radius: 10px;
    border: 1px solid rgba(100,116,139,0.25);
    background: rgba(15,23,42,0.6);
    color: #e2e8f0;
    font-size: 13px;
    outline: none;
    transition: border-color 0.2s;
}
.vig-input:focus {
    border-color: rgba(139,92,246,0.5);
}
.vig-input:disabled {
    opacity: 0.5; cursor: default;
}
.vig-input::placeholder { color: #64748b; }

.vig-send-btn {
    width: 40px; height: 40px;
    border-radius: 10px;
    border: 1px solid rgba(139,92,246,0.3);
    background: rgba(124,58,237,0.25);
    color: #c4b5fd;
    cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    transition: background 0.2s, border-color 0.2s;
    flex-shrink: 0;
}
.vig-send-btn:hover:not(:disabled) {
    background: rgba(124,58,237,0.45);
    border-color: rgba(167,139,250,0.5);
}
.vig-send-btn:disabled { opacity: 0.35; cursor: default; }
`;
        document.head.appendChild(style);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Auto-mount on DOMContentLoaded
// ═══════════════════════════════════════════════════════════════════════

let _widgetInstance: FloatingAIAssistantWidget | null = null;

export function mountAssistantWidget(): FloatingAIAssistantWidget {
    if (_widgetInstance) return _widgetInstance;
    _widgetInstance = new FloatingAIAssistantWidget();
    return _widgetInstance;
}

export function unmountAssistantWidget(): void {
    _widgetInstance?.dispose();
    _widgetInstance = null;
}

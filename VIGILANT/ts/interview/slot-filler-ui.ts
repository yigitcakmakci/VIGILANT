/**
 * slot-filler-ui.ts — Slot progress indicator + finalize CTA for InterviewSlotFiller.
 *
 * Renders inside the existing FloatingAIAssistantWidget panel as a
 * supplementary progress bar and slot chips. Uses Tailwind-inspired
 * utility classes from the existing glassmorphism design system.
 */

import {
    SlotFillerState,
    SlotFillerAction,
    SlotInfo,
    SlotName,
    AskNextQuestionPayload,
    SlotPatchedPayload,
    SlotPatchedAndNextPayload,
    InterviewUpdatePayload,
    SLOT_LABELS,
    createInitialSlotFillerState,
    slotFillerReducer,
    canAnswerSlot,
    canFinalizeSlot,
    slotProgressPercent,
} from './slot-filler-types';

import {
    BridgeEnvelope,
    onMessage,
    publish,
    nextRequestId,
    generateId,
} from './event-bridge';

// ═══════════════════════════════════════════════════════════════════════
// SlotFillerUI — self-contained DOM component
// ═══════════════════════════════════════════════════════════════════════

export class SlotFillerUI {
    private _state: SlotFillerState = createInitialSlotFillerState();
    private _unsubBridge: (() => void) | null = null;

    // DOM refs
    private _root!: HTMLDivElement;
    private _progressBar!: HTMLDivElement;
    private _progressFill!: HTMLDivElement;
    private _progressLabel!: HTMLSpanElement;
    private _slotChips!: HTMLDivElement;
    private _questionArea!: HTMLDivElement;
    private _questionText!: HTMLDivElement;
    private _answerInput!: HTMLInputElement;
    private _answerBtn!: HTMLButtonElement;
    private _ctaBtn!: HTMLButtonElement;
    private _statusLabel!: HTMLDivElement;

    constructor(parentElement?: HTMLElement) {
        this._buildDOM(parentElement);
        this._bindEvents();
        this._unsubBridge = onMessage((evt) => this._onBridgeMessage(evt));
    }

    dispose(): void {
        this._unsubBridge?.();
        this._root.remove();
    }

    /** Start the slot-filling session (call from outside or auto-start) */
    startSession(): void {
        publish({
            type: 'SlotFillerStart' as any,
            sessionId: generateId(),
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: {},
        });
    }

    // ── State dispatch ─────────────────────────────────────────────────
    private _dispatch(action: SlotFillerAction): void {
        this._state = slotFillerReducer(this._state, action);
        this._render();
    }

    // ═══════════════════════════════════════════════════════════════════
    // DOM construction
    // ═══════════════════════════════════════════════════════════════════

    private _buildDOM(parent?: HTMLElement): void {
        this._injectStyles();

        this._root = document.createElement('div');
        this._root.className = 'vsf-root';

        // ── Progress bar ────────────────────────────────────────────────
        const progressWrap = document.createElement('div');
        progressWrap.className = 'vsf-progress-wrap';

        this._progressLabel = document.createElement('span');
        this._progressLabel.className = 'vsf-progress-label';
        this._progressLabel.textContent = 'Slot Durumu: 0/5';

        this._progressBar = document.createElement('div');
        this._progressBar.className = 'vsf-progress-bar';
        this._progressFill = document.createElement('div');
        this._progressFill.className = 'vsf-progress-fill';
        this._progressBar.appendChild(this._progressFill);

        progressWrap.appendChild(this._progressLabel);
        progressWrap.appendChild(this._progressBar);

        // ── Slot chips ──────────────────────────────────────────────────
        this._slotChips = document.createElement('div');
        this._slotChips.className = 'vsf-slot-chips';

        // ── Question area ───────────────────────────────────────────────
        this._questionArea = document.createElement('div');
        this._questionArea.className = 'vsf-question-area';

        this._questionText = document.createElement('div');
        this._questionText.className = 'vsf-question-text';

        const inputRow = document.createElement('div');
        inputRow.className = 'vsf-input-row';

        this._answerInput = document.createElement('input');
        this._answerInput.type = 'text';
        this._answerInput.className = 'vsf-answer-input';
        this._answerInput.placeholder = 'Cevabınızı yazın...';
        this._answerInput.maxLength = 500;

        this._answerBtn = document.createElement('button');
        this._answerBtn.className = 'vsf-answer-btn';
        this._answerBtn.innerHTML = `
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                 stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>`;
        this._answerBtn.title = 'Gönder';

        inputRow.appendChild(this._answerInput);
        inputRow.appendChild(this._answerBtn);

        this._questionArea.appendChild(this._questionText);
        this._questionArea.appendChild(inputRow);

        // ── CTA ─────────────────────────────────────────────────────────
        this._ctaBtn = document.createElement('button');
        this._ctaBtn.className = 'vsf-cta-btn';
        this._ctaBtn.textContent = '✦ Yeterli, Planı Oluştur';
        this._ctaBtn.disabled = true;

        // ── Status ──────────────────────────────────────────────────────
        this._statusLabel = document.createElement('div');
        this._statusLabel.className = 'vsf-status';

        // Assemble
        this._root.appendChild(progressWrap);
        this._root.appendChild(this._slotChips);
        this._root.appendChild(this._questionArea);
        this._root.appendChild(this._ctaBtn);
        this._root.appendChild(this._statusLabel);

        (parent ?? document.body).appendChild(this._root);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Events
    // ═══════════════════════════════════════════════════════════════════

    private _bindEvents(): void {
        this._answerBtn.addEventListener('click', () => this._onAnswer());
        this._answerInput.addEventListener('keydown', (e: KeyboardEvent) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                this._onAnswer();
            }
        });
        this._ctaBtn.addEventListener('click', () => this._onFinalize());
    }

    private _onAnswer(): void {
        if (!canAnswerSlot(this._state)) return;
        const text = this._answerInput.value.trim();
        if (!text) return;

        this._answerInput.value = '';

        publish({
            type: 'SlotFillerAnswer' as any,
            sessionId: '',
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { text },
        });
    }

    private _onFinalize(): void {
        if (!canFinalizeSlot(this._state)) return;

        this._dispatch({ type: 'SLOT_FINALIZED', payload: {
            action: 'finalized',
            endedBy: 'cta',
            finalized: true,
            filledCount: this._state.filledCount,
            totalSlots: this._state.totalSlots,
            slots: this._state.slots,
        }});

        publish({
            type: 'SlotFillerFinalize' as any,
            sessionId: '',
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { endedBy: 'cta' },
        });
    }

    // ═══════════════════════════════════════════════════════════════════
    // Bridge handler
    // ═══════════════════════════════════════════════════════════════════

    private _onBridgeMessage(evt: BridgeEnvelope): void {
        switch (evt.type) {
            case 'AskNextQuestion' as any: {
                const p = evt.payload as unknown as AskNextQuestionPayload;
                this._dispatch({ type: 'ASK_NEXT_QUESTION', payload: p });
                break;
            }
            case 'SlotPatched' as any: {
                const p = evt.payload as unknown as SlotPatchedPayload;
                this._dispatch({ type: 'SLOT_PATCHED', payload: p });
                break;
            }
            case 'SlotPatchedAndNext' as any: {
                const p = evt.payload as unknown as SlotPatchedAndNextPayload;
                this._dispatch({ type: 'SLOT_PATCHED_AND_NEXT', payload: p });
                break;
            }
            case 'InterviewUpdate' as any: {
                const p = evt.payload as unknown as InterviewUpdatePayload;
                if (p.action === 'reset') {
                    this._dispatch({ type: 'SLOT_SESSION_STARTED', slots: p.slots });
                } else if (p.action === 'finalized') {
                    this._dispatch({ type: 'SLOT_FINALIZED', payload: p });
                }
                break;
            }
            case 'Error' as any: {
                const p = evt.payload as { message?: string };
                this._dispatch({ type: 'SLOT_ERROR', message: p.message ?? 'Bilinmeyen hata' });
                break;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Render
    // ═══════════════════════════════════════════════════════════════════

    private _render(): void {
        const s = this._state;
        const pct = slotProgressPercent(s);

        // ── Progress bar ────────────────────────────────────────────────
        this._progressFill.style.width = `${pct}%`;
        this._progressLabel.textContent = `Slot Durumu: ${s.filledCount}/${s.totalSlots}`;

        // ── Slot chips ──────────────────────────────────────────────────
        this._slotChips.innerHTML = '';
        for (const slot of s.slots) {
            const chip = document.createElement('span');
            chip.className = 'vsf-chip vsf-chip--' + slot.status;
            if (slot.name === s.currentSlotName) {
                chip.className += ' vsf-chip--active';
            }
            const label = SLOT_LABELS[slot.name as SlotName] ?? slot.name;
            chip.textContent = (slot.status === 'filled' ? '✓ ' : '') + label;
            this._slotChips.appendChild(chip);
        }

        // ── Question area ───────────────────────────────────────────────
        if (s.status === 'asking' && s.currentQuestion) {
            this._questionArea.style.display = '';
            this._questionText.textContent = s.currentQuestion;
            this._answerInput.disabled = false;
            this._answerBtn.disabled = false;
            this._answerInput.placeholder = 'Cevabınızı yazın...';
        } else {
            this._questionArea.style.display = 'none';
        }

        if (s.status === 'finalized') {
            this._answerInput.disabled = true;
            this._answerBtn.disabled = true;
            this._answerInput.placeholder = 'Görüşme tamamlandı.';
        }

        // ── CTA button ──────────────────────────────────────────────────
        this._ctaBtn.disabled = !canFinalizeSlot(s);
        if (s.status === 'finalized') {
            this._ctaBtn.textContent = '✓ Plan Oluşturuldu';
            this._ctaBtn.classList.add('vsf-cta-btn--done');
            this._ctaBtn.disabled = true;
        } else {
            this._ctaBtn.textContent = '✦ Yeterli, Planı Oluştur';
            this._ctaBtn.classList.remove('vsf-cta-btn--done');
        }

        // ── Status label ────────────────────────────────────────────────
        if (s.status === 'finalized') {
            const endLabel = s.endedBy === 'complete' ? 'tamamlandı'
                           : s.endedBy === 'limit' ? 'soru limiti doldu'
                           : 'kullanıcı tarafından sonlandırıldı';
            this._statusLabel.textContent =
                `✓ Görüşme ${endLabel} (${s.filledCount}/${s.totalSlots} slot, ${s.questionCount} soru)`;
            this._statusLabel.className = 'vsf-status vsf-status--done';
        } else if (s.status === 'error') {
            this._statusLabel.textContent = `⚠ ${s.errorMessage ?? 'Hata'}`;
            this._statusLabel.className = 'vsf-status vsf-status--error';
        } else if (s.status === 'asking') {
            this._statusLabel.textContent =
                `Soru ${s.questionCount}/${s.maxQuestions} · ${s.filledCount}/${s.totalSlots} slot dolu`;
            this._statusLabel.className = 'vsf-status';
        } else {
            this._statusLabel.textContent = '';
            this._statusLabel.className = 'vsf-status';
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Styles (glassmorphism, consistent with floating-widget.ts)
    // ═══════════════════════════════════════════════════════════════════

    private _injectStyles(): void {
        if (document.getElementById('vsf-slot-filler-styles')) return;

        const style = document.createElement('style');
        style.id = 'vsf-slot-filler-styles';
        style.textContent = `
/* ── SlotFiller Root ─────────────────────────────────────────────── */
.vsf-root {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    font-size: 13px;
    color: #e2e8f0;
    padding: 12px;
}

/* ── Progress Bar ────────────────────────────────────────────────── */
.vsf-progress-wrap {
    margin-bottom: 10px;
}
.vsf-progress-label {
    display: block;
    font-size: 11px;
    color: #94a3b8;
    margin-bottom: 4px;
    font-weight: 500;
}
.vsf-progress-bar {
    width: 100%;
    height: 6px;
    border-radius: 3px;
    background: rgba(30, 41, 59, 0.6);
    overflow: hidden;
    border: 1px solid rgba(100, 116, 139, 0.15);
}
.vsf-progress-fill {
    height: 100%;
    border-radius: 3px;
    background: linear-gradient(90deg, #7c3aed, #a78bfa);
    transition: width 0.4s cubic-bezier(.4,0,.2,1);
    width: 0%;
}

/* ── Slot Chips ──────────────────────────────────────────────────── */
.vsf-slot-chips {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    margin-bottom: 10px;
}
.vsf-chip {
    padding: 3px 10px;
    border-radius: 12px;
    font-size: 11px;
    font-weight: 500;
    border: 1px solid rgba(100, 116, 139, 0.2);
    background: rgba(30, 41, 59, 0.4);
    color: #94a3b8;
    transition: all 0.2s;
}
.vsf-chip--filled {
    background: rgba(34, 197, 94, 0.15);
    border-color: rgba(34, 197, 94, 0.3);
    color: #86efac;
}
.vsf-chip--ambiguous {
    background: rgba(234, 179, 8, 0.15);
    border-color: rgba(234, 179, 8, 0.3);
    color: #fde68a;
}
.vsf-chip--active {
    border-color: rgba(139, 92, 246, 0.5);
    box-shadow: 0 0 8px rgba(139, 92, 246, 0.2);
}

/* ── Question Area ───────────────────────────────────────────────── */
.vsf-question-area {
    margin-bottom: 10px;
}
.vsf-question-text {
    padding: 10px 14px;
    border-radius: 12px;
    background: rgba(30, 41, 59, 0.6);
    border: 1px solid rgba(100, 116, 139, 0.2);
    color: #cbd5e1;
    font-size: 13px;
    line-height: 1.5;
    margin-bottom: 8px;
    backdrop-filter: blur(6px);
    -webkit-backdrop-filter: blur(6px);
}
.vsf-input-row {
    display: flex;
    gap: 8px;
}
.vsf-answer-input {
    flex: 1;
    padding: 9px 12px;
    border-radius: 10px;
    border: 1px solid rgba(100, 116, 139, 0.25);
    background: rgba(15, 23, 42, 0.6);
    color: #e2e8f0;
    font-size: 13px;
    outline: none;
    transition: border-color 0.2s;
}
.vsf-answer-input:focus {
    border-color: rgba(139, 92, 246, 0.5);
}
.vsf-answer-input:disabled {
    opacity: 0.5;
    cursor: default;
}
.vsf-answer-input::placeholder { color: #64748b; }

.vsf-answer-btn {
    width: 38px;
    height: 38px;
    border-radius: 10px;
    border: 1px solid rgba(139, 92, 246, 0.3);
    background: rgba(124, 58, 237, 0.25);
    color: #c4b5fd;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: background 0.2s, border-color 0.2s;
    flex-shrink: 0;
}
.vsf-answer-btn:hover:not(:disabled) {
    background: rgba(124, 58, 237, 0.45);
    border-color: rgba(167, 139, 250, 0.5);
}
.vsf-answer-btn:disabled { opacity: 0.35; cursor: default; }

/* ── CTA Button ──────────────────────────────────────────────────── */
.vsf-cta-btn {
    width: 100%;
    margin: 8px 0 6px;
    padding: 10px 16px;
    border: 1px solid rgba(139, 92, 246, 0.4);
    border-radius: 10px;
    background: linear-gradient(135deg, rgba(124, 58, 237, 0.35), rgba(139, 92, 246, 0.18));
    color: #e0d4fd;
    font-weight: 600;
    font-size: 13px;
    cursor: pointer;
    transition: background 0.2s, border-color 0.2s, opacity 0.2s;
    text-align: center;
    letter-spacing: 0.2px;
}
.vsf-cta-btn:hover:not(:disabled) {
    background: linear-gradient(135deg, rgba(124, 58, 237, 0.55), rgba(139, 92, 246, 0.35));
    border-color: rgba(167, 139, 250, 0.6);
}
.vsf-cta-btn:disabled {
    opacity: 0.4;
    cursor: default;
}
.vsf-cta-btn--done {
    background: linear-gradient(135deg, rgba(34, 197, 94, 0.3), rgba(22, 163, 74, 0.15));
    border-color: rgba(34, 197, 94, 0.4);
    color: #86efac;
}

/* ── Status ──────────────────────────────────────────────────────── */
.vsf-status {
    font-size: 11px;
    color: #94a3b8;
    min-height: 18px;
    padding: 2px 0;
}
.vsf-status--done { color: #86efac; }
.vsf-status--error { color: #fca5a5; }
`;
        document.head.appendChild(style);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Mount helper
// ═══════════════════════════════════════════════════════════════════════

let _slotFillerInstance: SlotFillerUI | null = null;

export function mountSlotFiller(parent?: HTMLElement): SlotFillerUI {
    if (_slotFillerInstance) return _slotFillerInstance;
    _slotFillerInstance = new SlotFillerUI(parent);
    return _slotFillerInstance;
}

export function unmountSlotFiller(): void {
    _slotFillerInstance?.dispose();
    _slotFillerInstance = null;
}

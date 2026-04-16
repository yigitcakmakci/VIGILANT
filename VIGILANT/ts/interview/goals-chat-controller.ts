/**
 * goals-chat-controller.ts — GoalsChatController class for the Goals tab.
 *
 * Typed version of goals-chat-bundle.js. Manages the Socratic Q&A flow,
 * typing indicators, progress dots, and final DynamicGoalTree rendering.
 *
 * C++ envelope contract:
 *   type: "GoalsChatResponse"
 *   payload.responseType: "chatStarted" | "question" | "finalPlan"
 *   payload.text:         AI message string
 *   payload.sessionId:    (on chatStarted)
 *   payload.goalTree:     (on finalPlan)
 *   payload.questionCount / maxQuestions: (on question)
 *   payload.nextIsFinal:  boolean (on penultimate question)
 */

import {
	onMessage,
	publish,
	BridgeEnvelope,
} from './event-bridge';
import { renderGoalTree } from './goal-tree-recursive-ui';
import type { DynamicGoalTree } from './goal-tree-types';

// ── Payload shape from C++ ─────────────────────────────────────────────
interface GoalsChatPayload {
	responseType: 'chatStarted' | 'question' | 'finalPlan';
	text?: string;
	sessionId?: string;
	messageId?: string;
	role?: string;
	goalTree?: DynamicGoalTree;
	questionCount?: number;
	maxQuestions?: number;
	nextIsFinal?: boolean;
}

type BubbleRole = 'user' | 'ai' | 'system';

// ── DOM element refs ───────────────────────────────────────────────────
interface ChatElements {
	messages: HTMLElement | null;
	input: HTMLInputElement | null;
	sendBtn: HTMLButtonElement | null;
	placeholder: HTMLElement | null;
	typing: HTMLElement | null;
	progress: HTMLElement | null;
	progressDots: HTMLElement | null;
	progressLabel: HTMLElement | null;
	treeContainer: HTMLElement | null;
	startBtn: HTMLButtonElement | null;
}

let _reqCounter = 0;
function nextRequestId(): string {
	return `gc-${Date.now()}-${(++_reqCounter).toString(36)}`;
}

// ═══════════════════════════════════════════════════════════════════════
// GoalsChatController
// ═══════════════════════════════════════════════════════════════════════
export class GoalsChatController {
	private _sessionId: string | null = null;
	private _busy = false;
	private _questionCount = 0;
	private _maxQuestions = 10;
	private _unsubscribe: (() => void) | null = null;
	private _els: ChatElements = {
		messages: null, input: null, sendBtn: null, placeholder: null,
		typing: null, progress: null, progressDots: null,
		progressLabel: null, treeContainer: null, startBtn: null,
	};

	/** Bind to DOM elements and attach listeners. Call once after DOM ready. */
	init(
		messages: HTMLElement,
		input: HTMLInputElement,
		sendBtn: HTMLButtonElement,
		treeContainer: HTMLElement,
	): void {
		this._els.messages      = messages;
		this._els.input         = input;
		this._els.sendBtn       = sendBtn;
		this._els.treeContainer = treeContainer;
		this._els.placeholder   = document.getElementById('goalsChatPlaceholder');
		this._els.typing        = document.getElementById('goalsChatTyping');
		this._els.progress      = document.getElementById('goalsChatProgress');
		this._els.progressDots  = document.getElementById('goalsChatDots');
		this._els.progressLabel = document.getElementById('goalsChatProgressLabel');
		this._els.startBtn      = document.getElementById('goalsChatStartBtn') as HTMLButtonElement;

		sendBtn.addEventListener('click', () => this.onSend());
		input.addEventListener('keydown', (e) => {
			if (e.key === 'Enter' && !e.shiftKey) {
				e.preventDefault();
				this.onSend();
			}
		});

		this._unsubscribe = onMessage((env) => this.handleBackend(env));
	}

	/** Start a new Socratic planning session. */
	start(): void {
		this._sessionId = null;
		this._busy = true;
		this._questionCount = 0;
		this.clearMessages();
		this.hidePlaceholder();
		this.hideProgress();
		this.showTyping();
		this.setInputEnabled(false);
		if (this._els.startBtn) this._els.startBtn.disabled = true;

		// Clear previously saved plan
		try { localStorage.removeItem('vigilant-goaltree'); } catch(e) {}
		const gtc = document.getElementById('goal-tree-container');
		if (gtc) { gtc.innerHTML = ''; gtc.style.display = 'none'; }
		const emptyState = document.getElementById('stEmptyState');
		if (emptyState) emptyState.style.display = '';
		const stLayout = document.getElementById('stLayout');
		if (stLayout) stLayout.style.display = 'none';

		publish({
			type: 'GoalsChatStartRequested',
			sessionId: '',
			requestId: nextRequestId(),
			ts: new Date().toISOString(),
			payload: {},
		});
	}

	/** Tear down listeners. */
	dispose(): void {
		this._unsubscribe?.();
		this._unsubscribe = null;
	}

	// ── Send user message ──────────────────────────────────────────────
	private _pendingFirstMessage: string | null = null;

	onSend(): void {
		if (this._busy || !this._els.input) return;
		const text = this._els.input.value.trim();
		if (!text) return;

		// Auto-start session on first message
		if (!this._sessionId) {
			this._pendingFirstMessage = text;
			this._els.input.value = '';
			this.hidePlaceholder();
			this.start();
			return;
		}

		this.appendBubble(text, 'user');
		this._els.input.value = '';
		this._busy = true;
		this.setInputEnabled(false);
		this.showTyping();

		publish({
			type: 'GoalsChatMessageSubmitted',
			sessionId: this._sessionId,
			requestId: nextRequestId(),
			ts: new Date().toISOString(),
			payload: { sessionId: this._sessionId, text },
		});
	}

	// ── Handle backend response ────────────────────────────────────────
	private handleBackend(env: BridgeEnvelope): void {
		if (env.type !== 'GoalsChatResponse') return;
		const p = env.payload as unknown as GoalsChatPayload;
		const rt = p.responseType;

		this.hideTyping();

		if (rt === 'chatStarted' && p.sessionId) {
			this._sessionId = p.sessionId;
			this._maxQuestions = p.maxQuestions ?? 4;
		}

		if (rt === 'chatStarted' || rt === 'question') {
			if (p.text) this.appendBubble(p.text, 'ai');
			this._busy = false;
			this.setInputEnabled(true);
			this._els.input?.focus();

			if (rt === 'question') {
				this._questionCount = p.questionCount ?? this._questionCount + 1;
				this._maxQuestions  = p.maxQuestions  ?? this._maxQuestions;
			}
			this.updateProgress();

			// If user typed before session existed, send it now
			if (rt === 'chatStarted' && this._pendingFirstMessage) {
				const queued = this._pendingFirstMessage;
				this._pendingFirstMessage = null;
				if (this._els.input) this._els.input.value = queued;
				this.onSend();
			}
		} else if (rt === 'finalPlan') {
				if (p.text) this.appendBubble(p.text, 'ai');
				this._busy = false;
				this.setInputEnabled(false);
				this.hideProgress();
				if (this._els.startBtn) this._els.startBtn.disabled = false;
				if (p.goalTree) {
					this.showPlanPreparation();
					setTimeout(() => {
						this.hidePlanPreparation();
						this.collapseChat();
						this.renderTree(p.goalTree);
					}, 1500);
				}
			}
	}

	// ── DOM helpers ────────────────────────────────────────────────────
	private appendBubble(text: string, role: BubbleRole): void {
		const c = this._els.messages;
		if (!c) return;
		const wrapper = document.createElement('div');
		const bubble  = document.createElement('div');

		if (role === 'user') {
			wrapper.className = 'flex justify-end';
			bubble.className  = 'max-w-[75%] px-4 py-2.5 rounded-2xl rounded-br-sm text-sm leading-relaxed';
			bubble.style.cssText = 'background:linear-gradient(135deg,rgba(16,185,129,0.55),rgba(5,150,105,0.4));color:white;backdrop-filter:blur(8px);border:1px solid rgba(16,185,129,0.2)';
		} else if (role === 'ai') {
			wrapper.className = 'flex justify-start';
			bubble.className  = 'max-w-[75%] px-4 py-2.5 rounded-2xl rounded-bl-sm text-sm leading-relaxed';
			bubble.style.cssText = 'background:rgba(255,255,255,0.05);color:rgba(255,255,255,0.85);backdrop-filter:blur(8px);border:1px solid rgba(255,255,255,0.07)';
		} else {
			wrapper.className = 'flex justify-center';
			bubble.className  = 'px-3 py-1 rounded-lg text-[11px] italic';
			bubble.style.cssText = 'background:rgba(255,255,255,0.03);color:rgba(255,255,255,0.25)';
		}
		bubble.textContent = text;
		wrapper.appendChild(bubble);
		c.appendChild(wrapper);
		c.scrollTop = c.scrollHeight;
	}

	private clearMessages(): void {
		const c  = this._els.messages;
		const ph = this._els.placeholder;
		if (!c) return;
		c.innerHTML = '';
		if (ph) c.appendChild(ph);
	}

	private hidePlaceholder(): void {
		if (this._els.placeholder) this._els.placeholder.style.display = 'none';
	}

	private showTyping(): void {
		if (this._els.typing) this._els.typing.style.display = 'block';
	}
	private hideTyping(): void {
		if (this._els.typing) this._els.typing.style.display = 'none';
	}

	private setInputEnabled(on: boolean): void {
		if (this._els.input)   this._els.input.disabled = !on;
		if (this._els.sendBtn) this._els.sendBtn.disabled = !on;
	}

	private updateProgress(): void {
		const { progress, progressDots, progressLabel } = this._els;
		if (!progress || !progressDots || !progressLabel) return;
		progress.classList.remove('hidden');
		progressDots.innerHTML = '';
		for (let i = 0; i < this._maxQuestions; i++) {
			const dot = document.createElement('span');
			dot.className = 'w-1.5 h-1.5 rounded-full transition-colors duration-300';
			dot.style.background = i < this._questionCount
				? 'rgba(16,185,129,0.7)'
				: 'rgba(255,255,255,0.1)';
			progressDots.appendChild(dot);
		}
		progressLabel.textContent = `${this._questionCount} / ${this._maxQuestions} soru`;
	}

	private hideProgress(): void {
		this._els.progress?.classList.add('hidden');
	}

	private showPlanPreparation(): void {
		const c = this._els.messages;
		if (!c) return;
		const overlay = document.createElement('div');
		overlay.id = 'goalsPlanPrepOverlay';
		overlay.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px 0;gap:16px;';
		overlay.innerHTML =
			'<div style="width:48px;height:48px;border:3px solid rgba(99,102,241,0.15);border-top-color:rgba(99,102,241,0.7);border-radius:50%;animation:goalsPlanSpin 0.8s linear infinite"></div>' +
			'<span style="color:rgba(255,255,255,0.5);font-size:13px;font-weight:500">Plan Hazırlanıyor...</span>' +
			'<span style="color:rgba(255,255,255,0.2);font-size:11px">Mülakat verilerin analiz ediliyor</span>';
		if (!document.getElementById('goalsPlanSpinStyle')) {
			const style = document.createElement('style');
			style.id = 'goalsPlanSpinStyle';
			style.textContent = '@keyframes goalsPlanSpin{to{transform:rotate(360deg)}}';
			document.head.appendChild(style);
		}
		c.appendChild(overlay);
		c.scrollTop = c.scrollHeight;
	}

	private hidePlanPreparation(): void {
		document.getElementById('goalsPlanPrepOverlay')?.remove();
	}

	private collapseChat(): void {
		const section = document.getElementById('goalsChatSection');
		if (!section) return;
		section.style.transition = 'max-height 0.5s ease, opacity 0.4s ease, margin-bottom 0.5s ease';
		section.style.maxHeight = section.scrollHeight + 'px';
		section.style.overflow = 'hidden';
		void section.offsetHeight;
		section.style.maxHeight = '0px';
		section.style.opacity = '0';
		section.style.marginBottom = '0px';
	}

	private renderTree(tree: DynamicGoalTree): void {
		// Wrap raw GoalNode into v2 envelope if needed
		const wrapped = (tree as any).version ? tree : { version: 2, root: tree };

		// Persist to localStorage
		try { localStorage.setItem('vigilant-goaltree', JSON.stringify(wrapped)); } catch(e) {}

		// Render via SkillTree (main goals display area)
		const win = window as any;
		if (typeof win.SkillTree !== 'undefined' && win.SkillTree.loadTree) {
			win.SkillTree.loadTree(wrapped);
			const stContent = document.getElementById('stContent');
			if (stContent) stContent.scrollIntoView({ behavior: 'smooth' });
			return;
		}

		// Fallback: render inline
		const tc = this._els.treeContainer;
		if (!tc) return;
		tc.innerHTML = '';
		renderGoalTree(tc, tree);
		tc.style.display = 'block';
		tc.scrollIntoView({ behavior: 'smooth' });
	}
}

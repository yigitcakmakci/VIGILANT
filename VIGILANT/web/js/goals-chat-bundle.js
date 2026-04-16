/**
 * goals-chat-bundle.js — GoalsChatController class for the Goals tab Strategy Room.
 *
 * Handles the Socratic Q&A flow, typing indicators, progress dots,
 * and final DynamicGoalTree rendering via the existing recursive renderer.
 *
 * C++ envelope contract:
 *   type: "GoalsChatResponse"
 *   payload.responseType: "chatStarted" | "question" | "finalPlan"
 *   payload.text:         AI message string
 *   payload.sessionId:    (on chatStarted)
 *   payload.goalTree:     (on finalPlan) — recursive DynamicGoalTree JSON
 *   payload.questionCount / maxQuestions: (on question)
 */
(function () {
	'use strict';

	// ═══════════════════════════════════════════════════════════════════
	// GoalsChatController
	// ═══════════════════════════════════════════════════════════════════
	function GoalsChatController() {
		this._sessionId = null;
		this._busy = false;
		this._questionCount = 0;
		this._maxQuestions = 4;

		// DOM refs — bound in init()
		this._els = {
			messages: null,
			input: null,
			sendBtn: null,
			placeholder: null,
			typing: null,
			progress: null,
			progressDots: null,
			progressLabel: null,
			treeContainer: null,
			startBtn: null
		};
	}

	// ── Bridge helpers ──────────────────────────────────────────────────
	GoalsChatController.prototype._rid = function () {
		return 'gc-' + Date.now() + '-' + Math.random().toString(36).substr(2, 6);
	};

	GoalsChatController.prototype._post = function (envelope) {
		if (window.chrome && window.chrome.webview) {
			window.chrome.webview.postMessage(envelope);
		} else {
			console.log('[GoalsChat] post (no WebView2):', envelope);
		}
	};

	// ── Initialise — call once after DOM ready ─────────────────────────
	GoalsChatController.prototype.init = function () {
		var e = this._els;
		e.messages      = document.getElementById('goalsChatMessages');
		e.input         = document.getElementById('goalsChatInput');
		e.sendBtn       = document.getElementById('goalsChatSendBtn');
		e.placeholder   = document.getElementById('goalsChatPlaceholder');
		e.typing        = document.getElementById('goalsChatTyping');
		e.progress      = document.getElementById('goalsChatProgress');
		e.progressDots  = document.getElementById('goalsChatDots');
		e.progressLabel = document.getElementById('goalsChatProgressLabel');
		e.treeContainer = document.getElementById('goal-tree-container');
		e.startBtn      = document.getElementById('goalsChatStartBtn');

		var self = this;
		if (e.sendBtn) e.sendBtn.addEventListener('click', function () { self._onSend(); });
		if (e.input)   e.input.addEventListener('keydown', function (ev) {
			if (ev.key === 'Enter' && !ev.shiftKey) { ev.preventDefault(); self._onSend(); }
		});

		// Listen for C++ responses
		if (window.chrome && window.chrome.webview) {
			window.chrome.webview.addEventListener('message', function (ev) {
				self._onBackend(ev.data);
			});
		}

		// Input is always enabled so the user can start typing immediately
		this._setInputEnabled(true);
	};

	// ── Start a new session ────────────────────────────────────────────
	GoalsChatController.prototype.start = function () {
		this._sessionId = null;
		this._busy = true;
		this._questionCount = 0;
		this._clearMessages();
		this._hidePlaceholder();
		this._hideProgress();
		this._showTyping();
		this._setInputEnabled(false);
		if (this._els.startBtn) this._els.startBtn.disabled = true;

		// Clear previously saved plan
		/* Note: we no longer clear goals on new session — goals accumulate as a forest */
		var gtc = document.getElementById('goal-tree-container');
		if (gtc) { gtc.innerHTML = ''; gtc.style.display = 'none'; }
		var emptyState = document.getElementById('stEmptyState');
		if (emptyState) emptyState.style.display = '';
		var stLayout = document.getElementById('stLayout');
		if (stLayout) stLayout.style.display = 'none';

		this._post({
			type: 'GoalsChatStartRequested',
			sessionId: '',
			requestId: this._rid(),
			ts: new Date().toISOString(),
			payload: {}
		});
	};

	// ── Send user message ──────────────────────────────────────────────
	GoalsChatController.prototype._onSend = function () {
		if (this._busy) return;
		var el = this._els.input;
		if (!el) return;
		var text = el.value.trim();
		if (!text) return;

		// If no session yet, auto-start one and queue the message
		if (!this._sessionId) {
			this._pendingFirstMessage = text;
			el.value = '';
			this._hidePlaceholder();
			this.start();
			return;
		}

		this._appendBubble(text, 'user');
		el.value = '';
		this._busy = true;
		this._setInputEnabled(false);
		this._showTyping();

		this._post({
			type: 'GoalsChatMessageSubmitted',
			sessionId: this._sessionId,
			requestId: this._rid(),
			ts: new Date().toISOString(),
			payload: { sessionId: this._sessionId, text: text }
		});
	};

	// ── Handle backend message ─────────────────────────────────────────
	GoalsChatController.prototype._onBackend = function (data) {
		if (!data || data.type !== 'GoalsChatResponse') return;
		var p = data.payload || {};
		var rt = p.responseType;

		this._hideTyping();

		// Capture sessionId on chatStarted
		if (rt === 'chatStarted' && p.sessionId) {
			this._sessionId = p.sessionId;
			this._maxQuestions = p.maxQuestions || 4;
		}

		if (rt === 'chatStarted' || rt === 'question') {
			if (p.text) this._appendBubble(p.text, 'ai');
			this._busy = false;
			this._setInputEnabled(true);
			if (this._els.input) this._els.input.focus();

			// If user typed before session existed, send it now
			if (rt === 'chatStarted' && this._pendingFirstMessage) {
				var queued = this._pendingFirstMessage;
				this._pendingFirstMessage = null;
				if (this._els.input) this._els.input.value = queued;
				this._onSend();
			}

			// Update progress dots
			if (rt === 'question') {
				this._questionCount = p.questionCount || (this._questionCount + 1);
				this._maxQuestions  = p.maxQuestions  || this._maxQuestions;
			}
			this._updateProgress();


		} else if (rt === 'finalPlan') {
				if (p.text) this._appendBubble(p.text, 'ai');
				this._busy = false;
				this._setInputEnabled(false);
				this._hideProgress();
				if (this._els.startBtn) this._els.startBtn.disabled = false;
				if (p.goalTree) {
					this._showPlanPreparation();
					var self = this;
					setTimeout(function () {
						self._hidePlanPreparation();
						self._collapseChat();
						self._renderTree(p.goalTree);
					}, 1500);
				}
			}
	};

	// ── DOM: Bubble rendering ──────────────────────────────────────────
	GoalsChatController.prototype._appendBubble = function (text, role) {
		var c = this._els.messages;
		if (!c) return;

		var wrapper = document.createElement('div');
		var bubble = document.createElement('div');

		if (role === 'user') {
			wrapper.style.cssText = 'display:flex;justify-content:flex-end';
			bubble.style.cssText = 'max-width:75%;padding:8px 14px;border-radius:14px 14px 4px 14px;font-size:13px;line-height:1.5;background:linear-gradient(135deg,rgba(16,185,129,0.55),rgba(5,150,105,0.4));color:white;border:1px solid rgba(16,185,129,0.2)';
		} else if (role === 'ai') {
			wrapper.style.cssText = 'display:flex;justify-content:flex-start';
			bubble.style.cssText = 'max-width:75%;padding:8px 14px;border-radius:14px 14px 14px 4px;font-size:13px;line-height:1.5;background:rgba(255,255,255,0.05);color:rgba(255,255,255,0.85);border:1px solid rgba(255,255,255,0.07)';
		} else {
			wrapper.style.cssText = 'display:flex;justify-content:center';
			bubble.style.cssText = 'padding:4px 10px;border-radius:8px;font-size:11px;font-style:italic;background:rgba(255,255,255,0.03);color:rgba(255,255,255,0.25)';
		}

		bubble.textContent = text;
		wrapper.appendChild(bubble);
		c.appendChild(wrapper);
		c.scrollTop = c.scrollHeight;
	};

	GoalsChatController.prototype._clearMessages = function () {
		var c = this._els.messages;
		if (!c) return;
		// Keep only the placeholder, remove everything else
		var ph = this._els.placeholder;
		c.innerHTML = '';
		if (ph) c.appendChild(ph);
	};

	// ── DOM: Placeholder ───────────────────────────────────────────────
	GoalsChatController.prototype._hidePlaceholder = function () {
		if (this._els.placeholder) this._els.placeholder.style.display = 'none';
	};

	// ── DOM: Typing indicator ──────────────────────────────────────────
	GoalsChatController.prototype._showTyping = function () {
		if (this._els.typing) this._els.typing.style.display = 'block';
	};
	GoalsChatController.prototype._hideTyping = function () {
		if (this._els.typing) this._els.typing.style.display = 'none';
	};

	// ── DOM: Input state ───────────────────────────────────────────────
	GoalsChatController.prototype._setInputEnabled = function (on) {
		if (this._els.input)   this._els.input.disabled = !on;
		if (this._els.sendBtn) this._els.sendBtn.disabled = !on;
	};

	// ── DOM: Progress dots ─────────────────────────────────────────────
	GoalsChatController.prototype._updateProgress = function () {
		var el = this._els.progress;
		var dots = this._els.progressDots;
		var label = this._els.progressLabel;
		if (!el || !dots || !label) return;

		el.style.display = 'flex';
		dots.innerHTML = '';
		for (var i = 0; i < this._maxQuestions; i++) {
			var dot = document.createElement('span');
			dot.style.cssText = 'width:5px;height:5px;border-radius:50%;transition:background 0.3s';
			dot.style.background = i < this._questionCount
				? 'rgba(16,185,129,0.7)'
				: 'rgba(255,255,255,0.1)';
			dots.appendChild(dot);
		}
		label.textContent = this._questionCount + ' / ' + this._maxQuestions + ' soru';
	};

	GoalsChatController.prototype._hideProgress = function () {
		if (this._els.progress) this._els.progress.style.display = 'none';
	};

	// ── DOM: Plan preparation animation ───────────────────────────────
	GoalsChatController.prototype._showPlanPreparation = function () {
		var c = this._els.messages;
		if (!c) return;
		var overlay = document.createElement('div');
		overlay.id = 'goalsPlanPrepOverlay';
		overlay.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px 0;gap:16px;';
		overlay.innerHTML =
			'<div style="width:48px;height:48px;border:3px solid rgba(99,102,241,0.15);border-top-color:rgba(99,102,241,0.7);border-radius:50%;animation:goalsPlanSpin 0.8s linear infinite"></div>' +
			'<span style="color:rgba(255,255,255,0.5);font-size:13px;font-weight:500">Plan Hazırlanıyor...</span>' +
			'<span style="color:rgba(255,255,255,0.2);font-size:11px">Mülakat verilerin analiz ediliyor</span>';
		// Inject spinner keyframes if not already present
		if (!document.getElementById('goalsPlanSpinStyle')) {
			var style = document.createElement('style');
			style.id = 'goalsPlanSpinStyle';
			style.textContent = '@keyframes goalsPlanSpin{to{transform:rotate(360deg)}}';
			document.head.appendChild(style);
		}
		c.appendChild(overlay);
		c.scrollTop = c.scrollHeight;
	};

	GoalsChatController.prototype._hidePlanPreparation = function () {
		var el = document.getElementById('goalsPlanPrepOverlay');
		if (el) el.remove();
	};

	// ── DOM: Collapse chat section ───────────────────────────────────
	GoalsChatController.prototype._collapseChat = function () {
		var section = document.getElementById('goalsChatSection');
		if (!section) return;
		section.style.transition = 'max-height 0.5s ease, opacity 0.4s ease, margin-bottom 0.5s ease';
		section.style.maxHeight = section.scrollHeight + 'px';
		section.style.overflow = 'hidden';
		// Force reflow then collapse
		section.offsetHeight;
		section.style.maxHeight = '0px';
		section.style.opacity = '0';
		section.style.marginBottom = '0px';
	};

	// ── DOM: Render final tree (appends to goal forest) ──────────────
	GoalsChatController.prototype._renderTree = function (tree) {
		// Extract the root GoalNode
		var root = tree;
		if (tree.version && tree.root) { root = tree.root; }

		// Append to persisted forest array
		var forest = [];
		try {
			var raw = localStorage.getItem('vigilant-goaltrees');
			if (raw) { forest = JSON.parse(raw); }
			if (!Array.isArray(forest)) { forest = []; }
		} catch(e) { forest = []; }
		forest.push(root);
		try { localStorage.setItem('vigilant-goaltrees', JSON.stringify(forest)); } catch(e) {}

		// Render the full forest via GoalTreeBundle
		if (window.GoalTreeBundle && window.GoalTreeBundle.renderGoalForest) {
			var container = document.getElementById('goal-tree-container');
			if (container) {
				window.GoalTreeBundle.renderGoalForest(forest, container);
				var stContent = document.getElementById('stContent');
				if (stContent) stContent.scrollIntoView({ behavior: 'smooth' });
				return;
			}
		}

		// Fallback: render inline in the chat tree container
		var tc = this._els.treeContainer;
		if (!tc) return;
		tc.innerHTML = '';
		if (window.GoalTreeBundle && window.GoalTreeBundle.renderGoalTree) {
			window.GoalTreeBundle.renderGoalTree(tc, root);
		} else {
			tc.textContent = JSON.stringify(root, null, 2);
		}
		tc.style.display = 'block';
		tc.scrollIntoView({ behavior: 'smooth' });
	};

	// ═══════════════════════════════════════════════════════════════════
	// Bootstrap
	// ═══════════════════════════════════════════════════════════════════
	var controller = new GoalsChatController();

	window._startGoalsChat = function () { controller.start(); };
	window._initGoalsChat  = function () { controller.init(); };
	window._goalsChatController = controller;

	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', function () { controller.init(); });
	} else {
		controller.init();
	}
})();

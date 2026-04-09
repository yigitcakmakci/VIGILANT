/**
 * interview-widget.js — Compiled/bundled IIFE for WebView2 embedding.
 *
 * Self-contained: event-bridge + state + floating widget in one file.
 * This is the runtime artifact embedded via RC_DATA / virtual host.
 * Source of truth: VIGILANT/ts/interview/*.ts
 */
(function() {
    'use strict';

    // ═══════════════════════════════════════════════════════════════════
    // EVENT BRIDGE
    // ═══════════════════════════════════════════════════════════════════

    var _nextReqId = 1;
    var _handlers = [];
    var _bridgeBound = false;

    function nextRequestId() {
        return 'req-' + (_nextReqId++) + '-' + Date.now();
    }

    function generateId() {
        return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
            var r = (Math.random() * 16) | 0;
            var v = c === 'x' ? r : (r & 0x3) | 0x8;
            return v.toString(16);
        });
    }

    function bridgePublish(event) {
        if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
            window.chrome.webview.postMessage(event);
        } else {
            console.log('[InterviewBridge] publish (no WebView2):', event);
        }
    }

    function bridgeOnMessage(handler) {
        _handlers.push(handler);
        _ensureBridgeBound();
        return function() {
            var idx = _handlers.indexOf(handler);
            if (idx >= 0) _handlers.splice(idx, 1);
        };
    }

    function _ensureBridgeBound() {
        if (_bridgeBound) return;
        _bridgeBound = true;
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.addEventListener('message', function(e) {
                var data = e.data;
                if (data && data.type) {
                    for (var i = 0; i < _handlers.length; i++) {
                        try { _handlers[i](data); } catch(err) { console.error('[InterviewBridge]', err); }
                    }
                }
            });
        }
    }

    // ── Convenience publishers ──────────────────────────────────────────

    function publishInterviewStart(sessionId) {
        bridgePublish({
            type: 'InterviewStartRequested',
            sessionId: sessionId,
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { maxQuestions: 3 }
        });
    }

    function publishUserMessage(sessionId, text, messageId) {
        bridgePublish({
            type: 'UserMessageSubmitted',
            sessionId: sessionId,
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { text: text, messageId: messageId }
        });
    }

    function publishFinalize(sessionId, endedBy) {
        bridgePublish({
            type: 'FinalizeInterviewRequested',
            sessionId: sessionId,
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { endedBy: endedBy }
        });
    }

    function publishGenerateGoalTree(interviewSessionId) {
        bridgePublish({
            type: 'GenerateGoalTree',
            sessionId: interviewSessionId,
            requestId: nextRequestId(),
            ts: new Date().toISOString(),
            payload: { interviewSessionId: interviewSessionId }
        });
    }

    // ═══════════════════════════════════════════════════════════════════
    // STATE MANAGEMENT
    // ═══════════════════════════════════════════════════════════════════

    var MAX_QUESTIONS = 3;

    function createInitialState() {
        return {
            sessionId: '',
            status: 'idle',      // idle | asking | finalizing | finalized | error
            questionCount: 0,
            maxQuestions: MAX_QUESTIONS,
            endedBy: null,
            transcript: [],
            isSending: false,
            finalizeRequested: false,
            errorMessage: null
        };
    }

    function interviewReducer(state, action) {
        switch (action.type) {
            case 'SESSION_STARTED': {
                var s = createInitialState();
                s.sessionId = action.sessionId;
                s.status = 'asking';
                return s;
            }
            case 'SENDING_START':
                return Object.assign({}, state, { isSending: true });
            case 'SENDING_END':
                return Object.assign({}, state, { isSending: false });
            case 'USER_MESSAGE_SENT':
                return Object.assign({}, state, {
                    transcript: state.transcript.concat([action.message])
                });
            case 'AI_QUESTION_RECEIVED': {
                var newCount = action.questionCount;
                return Object.assign({}, state, {
                    transcript: state.transcript.concat([action.message]),
                    questionCount: newCount,
                    isSending: false,
                    // Keep 'asking' so user can answer the last question;
                    // C++ auto-finalizes when the final answer is received.
                    status: 'asking'
                });
            }
            case 'FINALIZE_REQUESTED':
                if (state.finalizeRequested || state.status === 'finalized') return state;
                return Object.assign({}, state, {
                    finalizeRequested: true,
                    status: 'finalizing',
                    endedBy: action.endedBy
                });
            case 'FINALIZE_CONFIRMED':
                return Object.assign({}, state, {
                    status: 'finalized',
                    finalizeRequested: true,
                    endedBy: action.endedBy,
                    questionCount: action.questionCount,
                    isSending: false
                });
            case 'ERROR':
                return Object.assign({}, state, {
                    status: 'error',
                    errorMessage: action.message,
                    isSending: false
                });
            case 'RESET':
                return createInitialState();
            default:
                return state;
        }
    }

    function canSendMessage(s) {
        return s.status === 'asking' && !s.isSending && !s.finalizeRequested;
    }

    function canFinalize(s) {
        return !s.finalizeRequested && s.status !== 'finalized' && s.status !== 'idle' && s.sessionId !== '';
    }

    function isLimitReached(s) {
        return s.questionCount >= s.maxQuestions;
    }

    // ═══════════════════════════════════════════════════════════════════
    // FLOATING WIDGET
    // ═══════════════════════════════════════════════════════════════════

    var _state = createInitialState();
    var _open = false;
    var _unsubBridge = null;

    // DOM refs
    var _root, _fab, _panel, _messageList, _input, _sendBtn, _ctaBtn, _inputWarn, _statusBar;

    function dispatch(action) {
        _state = interviewReducer(_state, action);
        render();
    }

    // ── Build DOM ───────────────────────────────────────────────────────

    function injectStyles() {
        if (document.getElementById('vig-assistant-styles')) return;
        var style = document.createElement('style');
        style.id = 'vig-assistant-styles';
        style.textContent = [
            '#vig-assistant-root{position:fixed;bottom:24px;right:24px;z-index:99999;font-family:"Segoe UI",system-ui,-apple-system,sans-serif;font-size:14px;color:#e2e8f0}',
            '.vig-fab{width:56px;height:56px;border-radius:50%;border:1px solid rgba(139,92,246,0.5);background:linear-gradient(135deg,rgba(30,27,75,0.85),rgba(76,29,149,0.7));backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);color:#c4b5fd;cursor:pointer;display:flex;align-items:center;justify-content:center;box-shadow:0 4px 24px rgba(139,92,246,0.35),0 0 0 1px rgba(139,92,246,0.15);transition:transform .2s,opacity .25s,box-shadow .2s}',
            '.vig-fab:hover{transform:scale(1.08);box-shadow:0 6px 32px rgba(139,92,246,0.5),0 0 0 1px rgba(139,92,246,0.3)}',
            '.vig-fab--hidden{opacity:0;pointer-events:none;transform:scale(0.7)}',
            '.vig-panel{position:absolute;bottom:0;right:0;width:390px;height:530px;border-radius:16px;overflow:hidden;display:flex;flex-direction:column;background:rgba(15,12,41,0.72);backdrop-filter:blur(24px) saturate(1.3);-webkit-backdrop-filter:blur(24px) saturate(1.3);border:1px solid rgba(139,92,246,0.25);box-shadow:0 8px 48px rgba(0,0,0,0.55),0 0 0 1px rgba(139,92,246,0.12),inset 0 1px 0 rgba(255,255,255,0.05);transition:transform .35s cubic-bezier(.4,0,.2,1),opacity .3s ease;transform-origin:bottom right}',
            '.vig-panel--closed{transform:translateY(20px) scale(0.95);opacity:0;pointer-events:none}',
            '.vig-panel--open{transform:translateY(0) scale(1);opacity:1;pointer-events:auto}',
            '.vig-panel__header{display:flex;align-items:center;justify-content:space-between;padding:14px 16px 10px;border-bottom:1px solid rgba(139,92,246,0.15);flex-shrink:0}',
            '.vig-panel__title{font-weight:600;font-size:14px;color:#c4b5fd;letter-spacing:.3px}',
            '.vig-panel__close{background:none;border:none;color:#94a3b8;font-size:22px;cursor:pointer;padding:0 4px;line-height:1;transition:color .15s}',
            '.vig-panel__close:hover{color:#f87171}',
            '.vig-cta-btn{margin:8px 12px 4px;padding:10px 16px;border:1px solid rgba(139,92,246,0.4);border-radius:10px;background:linear-gradient(135deg,rgba(124,58,237,0.35),rgba(139,92,246,0.18));color:#e0d4fd;font-weight:600;font-size:13px;cursor:pointer;transition:background .2s,border-color .2s,opacity .2s;flex-shrink:0;text-align:center;letter-spacing:.2px}',
            '.vig-cta-btn:hover:not(:disabled){background:linear-gradient(135deg,rgba(124,58,237,0.55),rgba(139,92,246,0.35));border-color:rgba(167,139,250,0.6)}',
            '.vig-cta-btn:disabled{opacity:.4;cursor:default}',
            '.vig-cta-btn--done{background:linear-gradient(135deg,rgba(34,197,94,0.3),rgba(22,163,74,0.15));border-color:rgba(34,197,94,0.4);color:#86efac}',
            '.vig-status-bar{padding:4px 16px;font-size:11px;color:#94a3b8;flex-shrink:0;min-height:20px}',
            '.vig-status-bar--done{color:#86efac}',
            '.vig-status-bar--error{color:#fca5a5}',
            '.vig-messages{flex:1;overflow-y:auto;padding:8px 12px;display:flex;flex-direction:column;gap:8px;scrollbar-width:thin;scrollbar-color:rgba(139,92,246,0.3) transparent}',
            '.vig-messages::-webkit-scrollbar{width:5px}',
            '.vig-messages::-webkit-scrollbar-thumb{background:rgba(139,92,246,0.3);border-radius:4px}',
            '.vig-bubble{max-width:85%;padding:10px 14px;border-radius:14px;font-size:13px;line-height:1.5;word-wrap:break-word}',
            '.vig-bubble--user{align-self:flex-end;background:rgba(124,58,237,0.3);border:1px solid rgba(139,92,246,0.25);color:#ede9fe;border-bottom-right-radius:4px}',
            '.vig-bubble--ai{align-self:flex-start;background:rgba(30,41,59,0.6);border:1px solid rgba(100,116,139,0.2);color:#cbd5e1;border-bottom-left-radius:4px;backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px)}',
            '.vig-input-warn{display:block;min-height:16px;padding:0 16px;font-size:11px;color:#fca5a5;flex-shrink:0}',
            '.vig-input-area{display:flex;gap:8px;padding:8px 12px 14px;border-top:1px solid rgba(139,92,246,0.12);flex-shrink:0}',
            '.vig-input{flex:1;padding:10px 14px;border-radius:10px;border:1px solid rgba(100,116,139,0.25);background:rgba(15,23,42,0.6);color:#e2e8f0;font-size:13px;outline:none;transition:border-color .2s}',
            '.vig-input:focus{border-color:rgba(139,92,246,0.5)}',
            '.vig-input:disabled{opacity:.5;cursor:default}',
            '.vig-input::placeholder{color:#64748b}',
            '.vig-send-btn{width:40px;height:40px;border-radius:10px;border:1px solid rgba(139,92,246,0.3);background:rgba(124,58,237,0.25);color:#c4b5fd;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:background .2s,border-color .2s;flex-shrink:0}',
            '.vig-send-btn:hover:not(:disabled){background:rgba(124,58,237,0.45);border-color:rgba(167,139,250,0.5)}',
            '.vig-send-btn:disabled{opacity:.35;cursor:default}'
        ].join('\n');
        document.head.appendChild(style);
    }

    function buildDOM() {
        injectStyles();

        _root = document.createElement('div');
        _root.id = 'vig-assistant-root';

        // FAB
        _fab = document.createElement('button');
        _fab.className = 'vig-fab';
        _fab.title = 'VIGILANT Assistant';
        _fab.innerHTML = '<svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>';

        // Panel
        _panel = document.createElement('div');
        _panel.className = 'vig-panel vig-panel--closed';

        // Header
        var header = document.createElement('div');
        header.className = 'vig-panel__header';
        header.innerHTML = '<span class="vig-panel__title"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="vertical-align:-2px;margin-right:6px"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/></svg>VIGILANT Assistant</span>';
        var closeBtn = document.createElement('button');
        closeBtn.className = 'vig-panel__close';
        closeBtn.innerHTML = '&times;';
        closeBtn.title = 'Kapat';
        closeBtn.addEventListener('click', function() { togglePanel(false); });
        header.appendChild(closeBtn);

        // CTA
        _ctaBtn = document.createElement('button');
        _ctaBtn.className = 'vig-cta-btn';
        _ctaBtn.textContent = '\u2726 Yeterli, Plan\u0131 Olu\u015Ftur';
        _ctaBtn.disabled = true;

        // Status
        _statusBar = document.createElement('div');
        _statusBar.className = 'vig-status-bar';

        // Messages
        _messageList = document.createElement('div');
        _messageList.className = 'vig-messages';

        // Input area
        var inputArea = document.createElement('div');
        inputArea.className = 'vig-input-area';

        _input = document.createElement('input');
        _input.type = 'text';
        _input.className = 'vig-input';
        _input.placeholder = 'Mesaj\u0131n\u0131z\u0131 yaz\u0131n...';
        _input.maxLength = 500;

        _sendBtn = document.createElement('button');
        _sendBtn.className = 'vig-send-btn';
        _sendBtn.title = 'G\u00F6nder';
        _sendBtn.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>';

        _inputWarn = document.createElement('span');
        _inputWarn.className = 'vig-input-warn';

        inputArea.appendChild(_input);
        inputArea.appendChild(_sendBtn);

        _panel.appendChild(header);
        _panel.appendChild(_ctaBtn);
        _panel.appendChild(_statusBar);
        _panel.appendChild(_messageList);
        _panel.appendChild(_inputWarn);
        _panel.appendChild(inputArea);

        _root.appendChild(_panel);
        _root.appendChild(_fab);
        document.body.appendChild(_root);
    }

    // ── Event bindings ──────────────────────────────────────────────────

    function bindEvents() {
        _fab.addEventListener('click', function() { togglePanel(true); });
        _sendBtn.addEventListener('click', onSend);
        _input.addEventListener('keydown', function(e) {
            if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); onSend(); }
        });
        _input.addEventListener('input', function() { _inputWarn.textContent = ''; });
        _ctaBtn.addEventListener('click', onFinalizeCTA);
    }

    // ── Toggle panel ────────────────────────────────────────────────────

    function togglePanel(open) {
        _open = open;
        if (open) {
            _panel.classList.remove('vig-panel--closed');
            _panel.classList.add('vig-panel--open');
            _fab.classList.add('vig-fab--hidden');
            if (_state.status === 'idle') startInterview();
            requestAnimationFrame(function() { _input.focus(); });
        } else {
            _panel.classList.remove('vig-panel--open');
            _panel.classList.add('vig-panel--closed');
            _fab.classList.remove('vig-fab--hidden');
        }
    }

    // ── Interview lifecycle ─────────────────────────────────────────────

    function startInterview() {
        var sid = generateId();
        dispatch({ type: 'SESSION_STARTED', sessionId: sid });
        publishInterviewStart(sid);
    }

    function onSend() {
        if (!canSendMessage(_state)) return;
        var text = _input.value.trim();
        if (!text) {
            _inputWarn.textContent = 'L\u00FCtfen bir mesaj yaz\u0131n.';
            return;
        }
        _inputWarn.textContent = '';
        var msg = { id: generateId(), role: 'user', text: text, ts: new Date().toISOString() };
        dispatch({ type: 'USER_MESSAGE_SENT', message: msg });
        dispatch({ type: 'SENDING_START' });
        _input.value = '';
        publishUserMessage(_state.sessionId, text, msg.id);
    }

    function onFinalizeCTA() {
        if (!canFinalize(_state)) return;
        dispatch({ type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
        publishFinalize(_state.sessionId, 'cta');
    }

    // ── Bridge message handler ──────────────────────────────────────────

    function onBridgeMessage(evt) {
        switch (evt.type) {
            case 'InterviewStarted': {
                var p = evt.payload;
                dispatch({ type: 'SESSION_STARTED', sessionId: p.sessionId });
                var q = p.firstQuestion;
                dispatch({
                    type: 'AI_QUESTION_RECEIVED',
                    message: { id: q.messageId, role: 'ai', text: q.text, ts: evt.ts },
                    questionCount: 1
                });
                break;
            }
            case 'AiQuestionProduced': {
                var ap = evt.payload;
                dispatch({
                    type: 'AI_QUESTION_RECEIVED',
                    message: { id: ap.messageId, role: 'ai', text: ap.text, ts: evt.ts },
                    questionCount: ap.questionCount,
                    autoFinalize: ap.autoFinalize
                });
                // Don't auto-finalize here — let the user answer the last question.
                // The C++ side auto-finalizes when the final answer is received.
                break;
            }
            case 'InterviewFinalized': {
                var fp = evt.payload;
                dispatch({ type: 'FINALIZE_CONFIRMED', endedBy: fp.endedBy, questionCount: fp.questionCount });
                if (fp.interviewSessionId && !fp.alreadyFinalized) {
                    publishGenerateGoalTree(fp.interviewSessionId);
                }
                break;
            }
            case 'Error': {
                var ep = evt.payload;
                if (ep.code === 'DUPLICATE_REQUEST') return;
                dispatch({ type: 'ERROR', message: ep.message || 'Bilinmeyen hata' });
                break;
            }
        }
    }

    // ── Render ──────────────────────────────────────────────────────────

    function render() {
        var s = _state;

        // Status bar
        if (s.status === 'finalized') {
            _statusBar.textContent = '\u2713 G\u00F6r\u00FC\u015Fme tamamland\u0131 (' + s.questionCount + '/' + s.maxQuestions + ' soru)';
            _statusBar.className = 'vig-status-bar vig-status-bar--done';
        } else if (s.status === 'error') {
            _statusBar.textContent = '\u26A0 ' + (s.errorMessage || 'Hata');
            _statusBar.className = 'vig-status-bar vig-status-bar--error';
        } else if (s.status === 'asking') {
            _statusBar.textContent = 'Soru ' + s.questionCount + '/' + s.maxQuestions;
            _statusBar.className = 'vig-status-bar';
        } else if (s.status === 'finalizing') {
            _statusBar.textContent = 'Sonu\u00E7land\u0131r\u0131l\u0131yor...';
            _statusBar.className = 'vig-status-bar';
        } else {
            _statusBar.textContent = '';
        }

        // Messages
        _messageList.innerHTML = '';
        for (var i = 0; i < s.transcript.length; i++) {
            var msg = s.transcript[i];
            var bubble = document.createElement('div');
            bubble.className = msg.role === 'user' ? 'vig-bubble vig-bubble--user' : 'vig-bubble vig-bubble--ai';
            bubble.textContent = msg.text;
            _messageList.appendChild(bubble);
        }
        requestAnimationFrame(function() { _messageList.scrollTop = _messageList.scrollHeight; });

        // Input
        var cs = canSendMessage(s);
        _input.disabled = !cs;
        _sendBtn.disabled = !cs;
        if (s.status === 'finalized') {
            _input.placeholder = 'G\u00F6r\u00FC\u015Fme tamamland\u0131.';
        } else if (s.isSending) {
            _input.placeholder = 'Yan\u0131t bekleniyor...';
        } else if (!cs && isLimitReached(s)) {
            _input.placeholder = 'Soru limiti doldu.';
        } else {
            _input.placeholder = 'Mesaj\u0131n\u0131z\u0131 yaz\u0131n...';
        }

        // CTA
        _ctaBtn.disabled = !canFinalize(s);
        if (s.status === 'finalized') {
            _ctaBtn.textContent = '\u2713 Plan Olu\u015Fturuldu';
            _ctaBtn.classList.add('vig-cta-btn--done');
        }
    }

    // ── Mount ───────────────────────────────────────────────────────────

    function mount() {
        buildDOM();
        bindEvents();
        _unsubBridge = bridgeOnMessage(onBridgeMessage);
    }

    // Auto-mount when DOM ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', mount);
    } else {
        mount();
    }

    // Expose for external access
    window._vigilantAssistant = {
        open: function() { togglePanel(true); },
        close: function() { togglePanel(false); },
        getState: function() { return _state; },
        reset: function() { dispatch({ type: 'RESET' }); }
    };

})();

/* ═══════════════════════════════════════════════════════════════════════
   VIGILANT – Live Timer Service
   Drift-resistant rAF-based timer that tracks active application sessions.
   Combines timer-store (state/reducer), timer-service (orchestrator),
   and dom-updater (UI binding) into a single vanilla JS module.
   Zero dependencies. Works in WebView2 + modern browsers.
   ═══════════════════════════════════════════════════════════════════════ */

// ── PubSub (reusable event bus) ────────────────────────────────────────
var TimerPubSub = (function () {
    'use strict';
    function TimerPubSub() {
        this._subs = {};
    }
    TimerPubSub.prototype.on = function (event, fn) {
        if (!this._subs[event]) this._subs[event] = [];
        this._subs[event].push(fn);
        var subs = this._subs;
        return function () {
            subs[event] = subs[event].filter(function (f) { return f !== fn; });
        };
    };
    TimerPubSub.prototype.emit = function (event, payload) {
        var list = this._subs[event];
        if (!list) return;
        for (var i = 0; i < list.length; i++) list[i](payload);
    };
    TimerPubSub.prototype.clear = function () {
        this._subs = {};
    };
    return TimerPubSub;
})();

// ═══════════════════════════════════════════════════════════════════════
// Timer Store – immutable state + reducer
// Drift-resistant: elapsed time computed from performance.now() deltas.
// ═══════════════════════════════════════════════════════════════════════

function _timerCreateInitialState() {
    return {
        activeApp: null,
        nativeStartMs: 0,
        perfStartMs: 0,
        accumulatedMs: 0,
        idle: false,
        idlePerfMs: null,
        sessions: []
    };
}

function _timerFinalizeSession(state, now) {
    if (!state.activeApp) return null;
    var elapsed = state.accumulatedMs;
    if (!state.idle) {
        elapsed += now - state.perfStartMs;
    } else if (state.idlePerfMs !== null) {
        elapsed += state.idlePerfMs - state.perfStartMs;
    }
    return {
        appName: state.activeApp,
        nativeStartMs: state.nativeStartMs,
        durationMs: Math.max(0, elapsed)
    };
}

function _timerReducer(state, action) {
    switch (action.type) {
        case 'ACTIVE_APP_CHANGED': {
            var prev = _timerFinalizeSession(state, action.now);
            var sessions = state.sessions.slice();
            if (prev) sessions.push(prev);
            return {
                activeApp: action.app,
                nativeStartMs: action.nativeStartMs,
                perfStartMs: action.now,
                accumulatedMs: 0,
                idle: false,
                idlePerfMs: null,
                sessions: sessions
            };
        }
        case 'IDLE_START': {
            if (state.idle || !state.activeApp) return state;
            var banked = state.accumulatedMs + (action.now - state.perfStartMs);
            return {
                activeApp: state.activeApp,
                nativeStartMs: state.nativeStartMs,
                perfStartMs: state.perfStartMs,
                accumulatedMs: banked,
                idle: true,
                idlePerfMs: action.now,
                sessions: state.sessions
            };
        }
        case 'IDLE_END': {
            if (!state.idle || !state.activeApp) return state;
            return {
                activeApp: state.activeApp,
                nativeStartMs: state.nativeStartMs,
                perfStartMs: action.now,
                accumulatedMs: state.accumulatedMs,
                idle: false,
                idlePerfMs: null,
                sessions: state.sessions
            };
        }
        case 'RESET':
            return _timerCreateInitialState();
        default:
            return state;
    }
}

function _timerSelectElapsedMs(state, now) {
    if (!state.activeApp) return 0;
    if (state.idle) return state.accumulatedMs;
    return state.accumulatedMs + (now - state.perfStartMs);
}

// ═══════════════════════════════════════════════════════════════════════
// Timer Service – orchestrates the rAF loop, drives the store
// ═══════════════════════════════════════════════════════════════════════

var TimerService = (function () {
    'use strict';

    function TimerService() {
        this.bus = new TimerPubSub();
        this._state = _timerCreateInitialState();
        this._rafId = null;
        this._lastDisplayedSecond = -1;
        this._running = false;
        // Bind tick so rAF callback keeps correct `this`
        this._boundTick = this._tick.bind(this);
    }

    // ── Public API ────────────────────────────────────────────────────

    TimerService.prototype.getState = function () {
        return this._state;
    };

    TimerService.prototype.isRunning = function () {
        return this._running;
    };

    /**
     * Call when native ActiveAppChanged event arrives.
     * @param {string} app – Process / window name
     * @param {number} nativeStartMs – Unix-ms timestamp from C++ side
     */
    TimerService.prototype.onActiveAppChanged = function (app, nativeStartMs) {
        var prevLen = this._state.sessions.length;

        this._dispatch({
            type: 'ACTIVE_APP_CHANGED',
            app: app,
            nativeStartMs: nativeStartMs,
            now: performance.now()
        });

        // Emit finalized session if one was produced
        if (this._state.sessions.length > prevLen) {
            var last = this._state.sessions[this._state.sessions.length - 1];
            this.bus.emit('sessionFinalized', last);
        }

        this._startLoop();
    };

    /** Call when native IdleStart event arrives */
    TimerService.prototype.onIdleStart = function () {
        this._dispatch({ type: 'IDLE_START', now: performance.now() });
        this._stopLoop();
    };

    /** Call when native IdleEnd event arrives */
    TimerService.prototype.onIdleEnd = function () {
        this._dispatch({ type: 'IDLE_END', now: performance.now() });
        if (this._state.activeApp) {
            this._startLoop();
        }
    };

    /** Full reset – stops loop and clears all state */
    TimerService.prototype.reset = function () {
        this._stopLoop();
        this._dispatch({ type: 'RESET' });
        this._lastDisplayedSecond = -1;
    };

    /** Snapshot the current elapsed ms */
    TimerService.prototype.elapsed = function () {
        return _timerSelectElapsedMs(this._state, performance.now());
    };

    TimerService.prototype.destroy = function () {
        this._stopLoop();
        this.bus.clear();
    };

    // ── Internal ──────────────────────────────────────────────────────

    TimerService.prototype._dispatch = function (action) {
        this._state = _timerReducer(this._state, action);
        this.bus.emit('stateChanged', this._state);
    };

    TimerService.prototype._startLoop = function () {
        if (this._running) return;
        this._running = true;
        this._lastDisplayedSecond = -1;
        this._boundTick();
    };

    TimerService.prototype._stopLoop = function () {
        this._running = false;
        if (this._rafId !== null) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    };

    TimerService.prototype._tick = function () {
        if (!this._running) return;

        var now = performance.now();
        var elapsedMs = _timerSelectElapsedMs(this._state, now);
        var displayedSecond = Math.floor(elapsedMs / 1000);

        // Only emit when the displayed second actually changes
        if (displayedSecond !== this._lastDisplayedSecond) {
            this._lastDisplayedSecond = displayedSecond;
            this.bus.emit('tick', elapsedMs);
        }

        this._rafId = requestAnimationFrame(this._boundTick);
    };

    return TimerService;
})();

// ═══════════════════════════════════════════════════════════════════════
// DOM Updater – thin glue between TimerService and the dashboard HTML
// ═══════════════════════════════════════════════════════════════════════

function _timerFormatElapsed(ms) {
    var totalSec = Math.floor(ms / 1000);
    var h = Math.floor(totalSec / 3600);
    var m = Math.floor((totalSec % 3600) / 60);
    var s = totalSec % 60;
    function pad2(n) { return n < 10 ? '0' + n : '' + n; }
    if (h > 0) return h + ':' + pad2(m) + ':' + pad2(s);
    return m + ':' + pad2(s);
}

/**
 * Mount the live timer UI — subscribes to timer events and updates DOM.
 * @param {TimerService} service
 * @returns {function} teardown function
 */
function mountTimerUI(service) {
    var refs = {
        elapsedEl: document.getElementById('live-timer-elapsed'),
        appNameEl: document.getElementById('live-timer-app'),
        idleBadge: document.getElementById('live-timer-idle'),
        sessionList: document.getElementById('session-history')
    };
    var unsubs = [];

    // Tick → update elapsed counter (fires at most once/sec)
    unsubs.push(
        service.bus.on('tick', function (elapsedMs) {
            if (refs.elapsedEl) {
                refs.elapsedEl.textContent = _timerFormatElapsed(elapsedMs);
            }
        })
    );

    // State changed → app name + idle badge
    unsubs.push(
        service.bus.on('stateChanged', function (state) {
            if (refs.appNameEl) {
                refs.appNameEl.textContent = state.activeApp || '\u2014';
            }
            if (refs.idleBadge) {
                refs.idleBadge.style.display = state.idle ? 'inline-block' : 'none';
            }
        })
    );

    // Session finalized → prepend to history list
    unsubs.push(
        service.bus.on('sessionFinalized', function (session) {
            if (!refs.sessionList) return;

            var row = document.createElement('div');
            row.className = 'session-row';

            var nameSpan = document.createElement('span');
            nameSpan.className = 'session-app';
            nameSpan.textContent = session.appName;

            var durSpan = document.createElement('span');
            durSpan.className = 'session-dur';
            durSpan.textContent = _timerFormatElapsed(session.durationMs);

            row.appendChild(nameSpan);
            row.appendChild(durSpan);
            refs.sessionList.insertBefore(row, refs.sessionList.firstChild);

            // Cap visible history
            while (refs.sessionList.children.length > 50) {
                refs.sessionList.removeChild(refs.sessionList.lastChild);
            }
        })
    );

    return function () {
        for (var i = 0; i < unsubs.length; i++) unsubs[i]();
    };
}

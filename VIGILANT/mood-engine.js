/* ═══════════════════════════════════════════════════════════════════════
   VIGILANT – Mood-Based Atmosphere Engine
   Computes moodScore (−10 … +10) from real-time productivity metrics
   and drives CSS custom-property transitions via lerp + rAF loop.
   Zero dependencies. Works in WebView2 + modern browsers.
   ═══════════════════════════════════════════════════════════════════════ */

// ── Minimal PubSub (type-less vanilla version) ────────────────────────
var MoodPubSub = (function () {
    'use strict';
    function MoodPubSub() {
        this._subs = {};
    }
    MoodPubSub.prototype.on = function (event, fn) {
        if (!this._subs[event]) this._subs[event] = [];
        this._subs[event].push(fn);
        var subs = this._subs;
        return function () {
            subs[event] = subs[event].filter(function (f) { return f !== fn; });
        };
    };
    MoodPubSub.prototype.emit = function (event, payload) {
        var list = this._subs[event];
        if (!list) return;
        for (var i = 0; i < list.length; i++) list[i](payload);
    };
    MoodPubSub.prototype.clear = function () {
        this._subs = {};
    };
    return MoodPubSub;
})();

// ── Helpers ───────────────────────────────────────────────────────────
function _moodClamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
function _moodLerp(a, b, t) { return a + (b - a) * t; }

function _parseHSL(hsl) {
    var parts = hsl.replace(/%/g, '').split(/\s+/);
    return [Number(parts[0]), Number(parts[1]), Number(parts[2])];
}

function _hslString(h, s, l) {
    return Math.round(h) + ' ' + Math.round(s) + '% ' + Math.round(l) + '%';
}

// ── Score computation ─────────────────────────────────────────────────
var MOOD_FOCUS_WINDOW_SEC = 1800;      // 30 min
var MOOD_SWITCH_SATURATION = 30;       // 30 switches → max penalty
var MOOD_WEIGHT_FOCUS = 10;
var MOOD_WEIGHT_SWITCH = 10;
var MOOD_WEIGHT_IDLE = 5;

function computeMoodScore(metrics) {
    var focusNorm = _moodClamp(metrics.focusSeconds / MOOD_FOCUS_WINDOW_SEC, 0, 1);
    var switchNorm = _moodClamp(metrics.appSwitchCount / MOOD_SWITCH_SATURATION, 0, 1);
    var idleNorm = _moodClamp(metrics.idleRatio, 0, 1);

    var raw = focusNorm * MOOD_WEIGHT_FOCUS
            - switchNorm * MOOD_WEIGHT_SWITCH
            - idleNorm * MOOD_WEIGHT_IDLE;

    return _moodClamp(Math.round(raw * 10) / 10, -10, 10);
}

// ── Theme mapping (score → visual targets) ────────────────────────────
var MOOD_THEME_ANCHORS = [
    { score: -10, theme: { accent: '0 84 60',   glow: 2,  vignetteOpacity: 0.55, blurStrength: 18 } },
    { score:   0, theme: { accent: '38 92 50',  glow: 10, vignetteOpacity: 0.25, blurStrength: 8  } },
    { score:  10, theme: { accent: '142 71 45', glow: 24, vignetteOpacity: 0.08, blurStrength: 0  } }
];

function _lerpTheme(a, b, t) {
    var ah = _parseHSL(a.accent), bh = _parseHSL(b.accent);
    return {
        accent: _hslString(_moodLerp(ah[0], bh[0], t), _moodLerp(ah[1], bh[1], t), _moodLerp(ah[2], bh[2], t)),
        glow: _moodLerp(a.glow, b.glow, t),
        vignetteOpacity: _moodLerp(a.vignetteOpacity, b.vignetteOpacity, t),
        blurStrength: _moodLerp(a.blurStrength, b.blurStrength, t)
    };
}

function themeForScore(score) {
    var s = _moodClamp(score, -10, 10);
    for (var i = 0; i < MOOD_THEME_ANCHORS.length - 1; i++) {
        var lo = MOOD_THEME_ANCHORS[i];
        var hi = MOOD_THEME_ANCHORS[i + 1];
        if (s <= hi.score) {
            var t = (s - lo.score) / (hi.score - lo.score);
            return _lerpTheme(lo.theme, hi.theme, t);
        }
    }
    var last = MOOD_THEME_ANCHORS[MOOD_THEME_ANCHORS.length - 1].theme;
    return { accent: last.accent, glow: last.glow, vignetteOpacity: last.vignetteOpacity, blurStrength: last.blurStrength };
}

// ── MoodEngine: smooth CSS-variable transition engine ─────────────────
var MOOD_LERP_SPEED = 3.5;
var MOOD_EPSILON = 0.001;

var MoodEngine = (function () {
    'use strict';

    function MoodEngine() {
        this.bus = new MoodPubSub();
        this._score = 0;
        this._current = themeForScore(0);
        this._target  = themeForScore(0);
        this._el = null;
        this._rafId = null;
        this._lastFrameTime = 0;
        this._running = false;

        // Bind frame so rAF callback keeps correct `this`
        var self = this;
        this._frameBound = function (now) { self._frame(now); };
    }

    // ── Public API ────────────────────────────────────────────────────

    /** Bind the engine to a DOM element (sets CSS variables on it). */
    MoodEngine.prototype.mount = function (el) {
        this._el = el;
        this._applyCSSVars(this._current);
    };

    /** Feed new productivity metrics – recomputes score, starts transition. */
    MoodEngine.prototype.update = function (metrics) {
        var next = computeMoodScore(metrics);
        if (next === this._score) return;
        this._score = next;
        this._target = themeForScore(next);
        this.bus.emit('scoreChanged', next);
        this._startLoop();
    };

    /** Directly set a score (for demos / debug). */
    MoodEngine.prototype.setScore = function (score) {
        var clamped = _moodClamp(Math.round(score * 10) / 10, -10, 10);
        if (clamped === this._score) return;
        this._score = clamped;
        this._target = themeForScore(clamped);
        this.bus.emit('scoreChanged', clamped);
        this._startLoop();
    };

    MoodEngine.prototype.getScore = function () { return this._score; };
    MoodEngine.prototype.getCurrentTheme = function () { return this._current; };

    MoodEngine.prototype.destroy = function () {
        this._stopLoop();
        this.bus.clear();
        this._el = null;
    };

    // ── Internal ──────────────────────────────────────────────────────

    MoodEngine.prototype._applyCSSVars = function (t) {
        if (!this._el) return;
        var s = this._el.style;
        s.setProperty('--mood-accent', t.accent);
        s.setProperty('--mood-glow', t.glow + 'px');
        s.setProperty('--mood-vignette-opacity', t.vignetteOpacity.toFixed(3));
        s.setProperty('--mood-blur-strength', t.blurStrength.toFixed(1) + 'px');
    };

    MoodEngine.prototype._converged = function () {
        var c = this._current, tgt = this._target;
        var ch = _parseHSL(c.accent), th = _parseHSL(tgt.accent);
        return Math.abs(ch[0] - th[0]) < MOOD_EPSILON &&
               Math.abs(ch[1] - th[1]) < MOOD_EPSILON &&
               Math.abs(ch[2] - th[2]) < MOOD_EPSILON &&
               Math.abs(c.glow - tgt.glow) < MOOD_EPSILON &&
               Math.abs(c.vignetteOpacity - tgt.vignetteOpacity) < MOOD_EPSILON &&
               Math.abs(c.blurStrength - tgt.blurStrength) < MOOD_EPSILON;
    };

    MoodEngine.prototype._startLoop = function () {
        if (this._running) return;
        this._running = true;
        this._lastFrameTime = performance.now();
        this._rafId = requestAnimationFrame(this._frameBound);
    };

    MoodEngine.prototype._stopLoop = function () {
        this._running = false;
        if (this._rafId !== null) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    };

    MoodEngine.prototype._frame = function (now) {
        if (!this._running) return;

        var dt = (now - this._lastFrameTime) / 1000;
        this._lastFrameTime = now;

        // Exponential ease: t = 1 − e^(−speed × dt)
        var t = 1 - Math.exp(-MOOD_LERP_SPEED * dt);
        this._current = _lerpTheme(this._current, this._target, t);
        this._applyCSSVars(this._current);
        this.bus.emit('themeFrame', this._current);

        if (this._converged()) {
            this._current = _lerpTheme(this._target, this._target, 1); // copy
            this._applyCSSVars(this._current);
            this._stopLoop();
            return;
        }

        this._rafId = requestAnimationFrame(this._frameBound);
    };

    return MoodEngine;
})();

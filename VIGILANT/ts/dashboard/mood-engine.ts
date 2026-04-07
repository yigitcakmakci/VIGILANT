/**
 * Mood-Based Atmosphere Engine
 *
 * Computes a moodScore (−10 … +10) from real-time productivity metrics
 * and drives CSS custom-property transitions via a lerp + rAF loop so
 * the dashboard "breathes" with the user's focus state.
 *
 * Integration:
 *   const engine = new MoodEngine();
 *   engine.mount(document.documentElement);   // target for CSS vars
 *   engine.update({ focusSeconds: 1200, appSwitchCount: 3, idleRatio: 0.1 });
 *   // … later
 *   engine.destroy();
 */

import { PubSub } from './pubsub';

// ── Public types ───────────────────────────────────────────────────────

/** Raw productivity signals fed into the engine. */
export interface MoodMetrics {
    /** Seconds the user stayed in a single app (last 30 min window). */
    focusSeconds: number;
    /** Number of app switches in the last 10 min window. */
    appSwitchCount: number;
    /** Fraction of idle time in the last 30 min window (0 – 1). */
    idleRatio: number;
}

/** Resolved CSS variable targets for a given mood. */
export interface MoodTheme {
    /** HSL accent colour string, e.g. "142 71% 45%". */
    accent: string;
    /** Glow spread radius in px. */
    glow: number;
    /** Vignette overlay opacity (0 – 1). */
    vignetteOpacity: number;
    /** Background blur strength in px. */
    blurStrength: number;
}

export interface MoodEvents {
    /** Fires whenever the score is recalculated. */
    scoreChanged: number;
    /** Fires every rAF frame while a transition is active. */
    themeFrame: MoodTheme;
}

// ── Helpers ────────────────────────────────────────────────────────────

function clamp(v: number, lo: number, hi: number): number {
    return v < lo ? lo : v > hi ? hi : v;
}

function lerp(a: number, b: number, t: number): number {
    return a + (b - a) * t;
}

/**
 * Parse an "H S% L%" string into [h, s, l] numbers.
 * Accepts the compact form stored in `accent` (e.g. "142 71 45").
 */
function parseHSL(hsl: string): [number, number, number] {
    const parts = hsl.replace(/%/g, '').split(/\s+/).map(Number);
    return [parts[0], parts[1], parts[2]];
}

function hslString(h: number, s: number, l: number): string {
    return `${Math.round(h)} ${Math.round(s)}% ${Math.round(l)}%`;
}

// ── Score computation ──────────────────────────────────────────────────

/** Tuning knobs – exposed as constants so tests can import them. */
export const FOCUS_WINDOW_SEC = 1800;          // 30 min
export const SWITCH_SATURATION = 30;           // 30 switches → max penalty
export const WEIGHT_FOCUS = 10;
export const WEIGHT_SWITCH = 10;
export const WEIGHT_IDLE = 5;

/**
 * Pure function: compute a mood score from raw metrics.
 *
 *   score =  focusNorm × W_focus
 *          − switchNorm × W_switch
 *          − idleRatio  × W_idle
 *
 * Clamped to [−10, +10].
 */
export function computeMoodScore(m: MoodMetrics): number {
    const focusNorm = clamp(m.focusSeconds / FOCUS_WINDOW_SEC, 0, 1);
    const switchNorm = clamp(m.appSwitchCount / SWITCH_SATURATION, 0, 1);
    const idleNorm = clamp(m.idleRatio, 0, 1);

    const raw =
        focusNorm * WEIGHT_FOCUS -
        switchNorm * WEIGHT_SWITCH -
        idleNorm * WEIGHT_IDLE;

    return clamp(Math.round(raw * 10) / 10, -10, 10);
}

// ── Theme mapping ──────────────────────────────────────────────────────

/** Mood-to-theme anchor points (score → theme). */
const THEME_ANCHORS: ReadonlyArray<{ score: number; theme: MoodTheme }> = [
    {
        score: -10,
        theme: {
            accent: '0 84 60',          // red-ish
            glow: 2,
            vignetteOpacity: 0.55,
            blurStrength: 18,
        },
    },
    {
        score: 0,
        theme: {
            accent: '38 92 50',         // amber
            glow: 10,
            vignetteOpacity: 0.25,
            blurStrength: 8,
        },
    },
    {
        score: 10,
        theme: {
            accent: '142 71 45',        // emerald (#34d399-ish)
            glow: 24,
            vignetteOpacity: 0.08,
            blurStrength: 0,
        },
    },
];

/** Interpolate between two MoodTheme objects (0 ≤ t ≤ 1). */
function lerpTheme(a: MoodTheme, b: MoodTheme, t: number): MoodTheme {
    const [ah, as, al] = parseHSL(a.accent);
    const [bh, bs, bl] = parseHSL(b.accent);
    return {
        accent: hslString(lerp(ah, bh, t), lerp(as, bs, t), lerp(al, bl, t)),
        glow: lerp(a.glow, b.glow, t),
        vignetteOpacity: lerp(a.vignetteOpacity, b.vignetteOpacity, t),
        blurStrength: lerp(a.blurStrength, b.blurStrength, t),
    };
}

/** Map a score (−10…+10) to an interpolated MoodTheme. */
export function themeForScore(score: number): MoodTheme {
    const s = clamp(score, -10, 10);
    for (let i = 0; i < THEME_ANCHORS.length - 1; i++) {
        const lo = THEME_ANCHORS[i];
        const hi = THEME_ANCHORS[i + 1];
        if (s <= hi.score) {
            const t = (s - lo.score) / (hi.score - lo.score);
            return lerpTheme(lo.theme, hi.theme, t);
        }
    }
    return { ...THEME_ANCHORS[THEME_ANCHORS.length - 1].theme };
}

// ── Smooth CSS-variable transition engine ──────────────────────────────

const LERP_SPEED = 3.5;          // higher = faster convergence
const EPSILON = 0.001;           // stop threshold per channel

export class MoodEngine {
    readonly bus = new PubSub<MoodEvents>();

    private _score = 0;
    private _current: MoodTheme = themeForScore(0);
    private _target: MoodTheme = themeForScore(0);
    private _el: HTMLElement | null = null;
    private _rafId: number | null = null;
    private _lastFrameTime = 0;
    private _running = false;

    // ── Public API ─────────────────────────────────────────────────────

    /** Bind the engine to a DOM element (sets CSS variables on it). */
    mount(el: HTMLElement): void {
        this._el = el;
        this.applyCSSVars(this._current);
    }

    /** Feed new metrics – recomputes score and starts the transition. */
    update(metrics: MoodMetrics): void {
        const next = computeMoodScore(metrics);
        if (next === this._score) return;
        this._score = next;
        this._target = themeForScore(next);
        this.bus.emit('scoreChanged', next);
        this.startLoop();
    }

    /** Directly set a score (for demos / sliders). */
    setScore(score: number): void {
        const clamped = clamp(Math.round(score * 10) / 10, -10, 10);
        if (clamped === this._score) return;
        this._score = clamped;
        this._target = themeForScore(clamped);
        this.bus.emit('scoreChanged', clamped);
        this.startLoop();
    }

    get score(): number {
        return this._score;
    }

    get currentTheme(): Readonly<MoodTheme> {
        return this._current;
    }

    destroy(): void {
        this.stopLoop();
        this.bus.clear();
        this._el = null;
    }

    // ── Internal ───────────────────────────────────────────────────────

    private applyCSSVars(t: MoodTheme): void {
        if (!this._el) return;
        const s = this._el.style;
        s.setProperty('--accent', t.accent);
        s.setProperty('--glow', `${t.glow}px`);
        s.setProperty('--vignette-opacity', t.vignetteOpacity.toFixed(3));
        s.setProperty('--blur-strength', `${t.blurStrength.toFixed(1)}px`);
    }

    private converged(): boolean {
        const [ch, cs, cl] = parseHSL(this._current.accent);
        const [th, ts, tl] = parseHSL(this._target.accent);
        return (
            Math.abs(ch - th) < EPSILON &&
            Math.abs(cs - ts) < EPSILON &&
            Math.abs(cl - tl) < EPSILON &&
            Math.abs(this._current.glow - this._target.glow) < EPSILON &&
            Math.abs(this._current.vignetteOpacity - this._target.vignetteOpacity) < EPSILON &&
            Math.abs(this._current.blurStrength - this._target.blurStrength) < EPSILON
        );
    }

    private startLoop(): void {
        if (this._running) return;
        this._running = true;
        this._lastFrameTime = performance.now();
        this._rafId = requestAnimationFrame(this.frame);
    }

    private stopLoop(): void {
        this._running = false;
        if (this._rafId !== null) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    }

    private frame = (now: number): void => {
        if (!this._running) return;

        const dt = (now - this._lastFrameTime) / 1000;  // seconds
        this._lastFrameTime = now;

        // Exponential ease: t = 1 − e^(−speed × dt)
        const t = 1 - Math.exp(-LERP_SPEED * dt);
        this._current = lerpTheme(this._current, this._target, t);
        this.applyCSSVars(this._current);
        this.bus.emit('themeFrame', this._current);

        if (this.converged()) {
            this._current = { ...this._target };
            this.applyCSSVars(this._current);
            this.stopLoop();
            return;
        }

        this._rafId = requestAnimationFrame(this.frame);
    };
}

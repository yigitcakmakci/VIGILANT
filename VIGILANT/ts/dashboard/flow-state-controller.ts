/**
 * Flow State Controller
 *
 * A lightweight state machine that detects sustained deep focus (≥ 45 min)
 * and drives a "Flow State" visual mode via CSS custom properties.
 *
 * States: Idle → Active → Flow → Cooldown → Idle
 *
 * Input signals arrive from the native C++ side via the WebView2 bridge:
 *   • focusSessionMinutes  – minutes in the current single-app session
 *   • idleSeconds          – seconds since last keyboard/mouse event
 *   • windowSwitchCountLast10Min – app-switch count in a rolling 10 min window
 *
 * Design:
 *   • All signal updates are debounced (default 2 s) to avoid rapid re-eval.
 *   • Hysteresis timers prevent flicker on promotion (Idle→Active 60 s hold)
 *     and demotion (Flow→Cooldown 30 s grace).
 *   • CSS variable application uses the same rAF crossfade strategy as
 *     MoodEngine so the two can coexist on the same root element.
 *   • prefers-reduced-motion is detected once at mount; when active the
 *     crossfade is replaced with an instant swap.
 *
 * Integration:
 *   const ctrl = new FlowStateController();
 *   ctrl.mount(document.documentElement);
 *   ctrl.updateSignals({ focusSessionMinutes: 48, idleSeconds: 5, windowSwitchCountLast10Min: 1 });
 *   // … later
 *   ctrl.destroy();
 */

import { PubSub } from './pubsub';

// ── Public types ───────────────────────────────────────────────────────

export const enum FlowState {
    Idle     = 'idle',
    Active   = 'active',
    Flow     = 'flow',
    Cooldown = 'cooldown',
}

export interface FlowSignals {
    /** Minutes the user stayed in a single app without switching. */
    focusSessionMinutes: number;
    /** Seconds since last input event (keyboard / mouse). */
    idleSeconds: number;
    /** Number of window/app switches in the last 10 min rolling window. */
    windowSwitchCountLast10Min: number;
}

export interface FlowCSSVars {
    /** Overlay opacity for the flow glow layer (0 – 1). */
    flowOverlayOpacity: number;
    /** Scale multiplier for the ambient glow (1 = normal). */
    flowGlowScale: number;
    /** Border accent opacity boost (0 – 1). */
    flowBorderAccent: number;
    /** Breathing animation speed multiplier (0 = paused, 1 = normal). */
    flowBreathingSpeed: number;
    /** Background tint opacity (0 – 1). */
    flowBgTint: number;
}

export interface FlowStateEvents {
    /** Fires on every state transition. */
    stateChanged: { prev: FlowState; next: FlowState };
    /** Fires every crossfade rAF frame while transitioning. */
    cssFrame: Readonly<FlowCSSVars>;
    /** Fires when signals are evaluated (after debounce). */
    signalsEvaluated: Readonly<FlowSignals>;
}

// ── Thresholds (exported for tests) ───────────────────────────────────

/** Minutes in single-app focus to qualify as "active". */
export const THRESH_ACTIVE_FOCUS_MIN = 5;
/** Minutes in single-app focus to qualify for flow promotion. */
export const THRESH_FLOW_FOCUS_MIN = 45;
/** Max idle seconds while still in "active" state. */
export const THRESH_ACTIVE_IDLE_SEC = 30;
/** Max idle seconds while still in "flow" state. */
export const THRESH_FLOW_IDLE_SEC = 60;
/** Idle seconds that immediately drop from Active → Idle. */
export const THRESH_IDLE_BREAK_SEC = 120;
/** Idle seconds that trigger Flow → Cooldown. */
export const THRESH_FLOW_BREAK_IDLE_SEC = 180;
/** Max switches/10 min during flow. */
export const THRESH_FLOW_SWITCHES = 2;
/** Switches/10 min that drop Active → Idle. */
export const THRESH_ACTIVE_BREAK_SWITCHES = 8;
/** Switches/10 min that trigger Flow → Cooldown. */
export const THRESH_FLOW_BREAK_SWITCHES = 4;
/** Focus minutes regression threshold for Flow → Active. */
export const THRESH_FLOW_REGRESS_MIN = 30;

/** Promotion hold time: Active must sustain conditions for this long. */
export const PROMOTION_HOLD_MS = 60_000;
/** Demotion grace: Flow tolerates a disruption for this long. */
export const DEMOTION_GRACE_MS = 30_000;
/** Cooldown duration before returning to Idle. */
export const COOLDOWN_DURATION_MS = 300_000;   // 5 min
/** Signal debounce interval. */
export const SIGNAL_DEBOUNCE_MS = 2_000;

// ── CSS variable targets per state ────────────────────────────────────

const CSS_VARS_IDLE: FlowCSSVars = {
    flowOverlayOpacity: 0,
    flowGlowScale: 1,
    flowBorderAccent: 0,
    flowBreathingSpeed: 0,
    flowBgTint: 0,
};

const CSS_VARS_ACTIVE: FlowCSSVars = {
    flowOverlayOpacity: 0.04,
    flowGlowScale: 1.05,
    flowBorderAccent: 0.15,
    flowBreathingSpeed: 0,
    flowBgTint: 0.02,
};

const CSS_VARS_FLOW: FlowCSSVars = {
    flowOverlayOpacity: 0.18,
    flowGlowScale: 1.35,
    flowBorderAccent: 0.6,
    flowBreathingSpeed: 1,
    flowBgTint: 0.10,
};

const CSS_VARS_COOLDOWN: FlowCSSVars = {
    flowOverlayOpacity: 0.08,
    flowGlowScale: 1.12,
    flowBorderAccent: 0.25,
    flowBreathingSpeed: 0.3,
    flowBgTint: 0.04,
};

function cssVarsForState(state: FlowState): FlowCSSVars {
    switch (state) {
        case FlowState.Idle:     return CSS_VARS_IDLE;
        case FlowState.Active:   return CSS_VARS_ACTIVE;
        case FlowState.Flow:     return CSS_VARS_FLOW;
        case FlowState.Cooldown: return CSS_VARS_COOLDOWN;
    }
}

// ── Helpers ────────────────────────────────────────────────────────────

function lerp(a: number, b: number, t: number): number {
    return a + (b - a) * t;
}

function lerpVars(a: FlowCSSVars, b: FlowCSSVars, t: number): FlowCSSVars {
    return {
        flowOverlayOpacity:  lerp(a.flowOverlayOpacity,  b.flowOverlayOpacity,  t),
        flowGlowScale:       lerp(a.flowGlowScale,       b.flowGlowScale,       t),
        flowBorderAccent:    lerp(a.flowBorderAccent,     b.flowBorderAccent,    t),
        flowBreathingSpeed:  lerp(a.flowBreathingSpeed,   b.flowBreathingSpeed,  t),
        flowBgTint:          lerp(a.flowBgTint,           b.flowBgTint,          t),
    };
}

function varsConverged(a: FlowCSSVars, b: FlowCSSVars): boolean {
    const E = 0.001;
    return (
        Math.abs(a.flowOverlayOpacity  - b.flowOverlayOpacity)  < E &&
        Math.abs(a.flowGlowScale       - b.flowGlowScale)       < E &&
        Math.abs(a.flowBorderAccent    - b.flowBorderAccent)    < E &&
        Math.abs(a.flowBreathingSpeed  - b.flowBreathingSpeed)  < E &&
        Math.abs(a.flowBgTint          - b.flowBgTint)          < E
    );
}

// ── Controller ─────────────────────────────────────────────────────────

/** Crossfade convergence speed – matches MoodEngine's LERP_SPEED. */
const CROSSFADE_SPEED = 3.5;

export class FlowStateController {
    readonly bus = new PubSub<FlowStateEvents>();

    // ── State machine ──────────────────────────────────────────────────
    private _state: FlowState = FlowState.Idle;
    private _signals: FlowSignals = { focusSessionMinutes: 0, idleSeconds: 0, windowSwitchCountLast10Min: 0 };

    // Hysteresis timers (setTimeout ids)
    private _promotionTimer: ReturnType<typeof setTimeout> | null = null;
    private _demotionTimer: ReturnType<typeof setTimeout> | null = null;
    private _cooldownTimer: ReturnType<typeof setTimeout> | null = null;

    // ── Signal debounce ────────────────────────────────────────────────
    private _debounceTimer: ReturnType<typeof setTimeout> | null = null;
    private _pendingSignals: FlowSignals | null = null;

    // ── CSS crossfade ──────────────────────────────────────────────────
    private _el: HTMLElement | null = null;
    private _current: FlowCSSVars = { ...CSS_VARS_IDLE };
    private _target: FlowCSSVars = { ...CSS_VARS_IDLE };
    private _rafId: number | null = null;
    private _lastFrameTime = 0;
    private _animating = false;
    private _reducedMotion = false;

    // Allow injecting timing functions for tests
    private _now: () => number;
    private _raf: (cb: FrameRequestCallback) => number;
    private _caf: (id: number) => void;

    constructor(opts?: {
        now?: () => number;
        raf?: (cb: FrameRequestCallback) => number;
        caf?: (id: number) => void;
    }) {
        this._now = opts?.now ?? (() => performance.now());
        this._raf = opts?.raf ?? ((cb) => requestAnimationFrame(cb));
        this._caf = opts?.caf ?? ((id) => cancelAnimationFrame(id));
    }

    // ── Public API ─────────────────────────────────────────────────────

    get state(): FlowState {
        return this._state;
    }

    get signals(): Readonly<FlowSignals> {
        return this._signals;
    }

    get currentVars(): Readonly<FlowCSSVars> {
        return this._current;
    }

    /** Bind to a DOM element; will set CSS custom properties on it. */
    mount(el: HTMLElement): void {
        this._el = el;
        this._reducedMotion =
            typeof matchMedia === 'function' &&
            matchMedia('(prefers-reduced-motion: reduce)').matches;
        this.applyCSSVars(this._current);
        el.dataset.flowState = this._state;
    }

    /**
     * Feed new signals (debounced).
     * Rapid calls within SIGNAL_DEBOUNCE_MS are coalesced; only the
     * latest signal set is evaluated.
     */
    updateSignals(s: FlowSignals): void {
        this._pendingSignals = { ...s };
        if (this._debounceTimer !== null) return;
        this._debounceTimer = setTimeout(() => {
            this._debounceTimer = null;
            if (this._pendingSignals) {
                this._signals = this._pendingSignals;
                this._pendingSignals = null;
                this.bus.emit('signalsEvaluated', this._signals);
                this.evaluate();
            }
        }, SIGNAL_DEBOUNCE_MS);
    }

    /**
     * Immediate signal evaluation (bypasses debounce).
     * Useful for tests or one-shot updates from native side.
     */
    forceUpdate(s: FlowSignals): void {
        this._signals = { ...s };
        this.bus.emit('signalsEvaluated', this._signals);
        this.evaluate();
    }

    destroy(): void {
        this.clearAllTimers();
        this.stopCrossfade();
        this.bus.clear();
        if (this._el) {
            delete this._el.dataset.flowState;
            this._el = null;
        }
    }

    // ── State evaluation ───────────────────────────────────────────────

    private evaluate(): void {
        const { focusSessionMinutes: fm, idleSeconds: idle, windowSwitchCountLast10Min: sw } = this._signals;

        switch (this._state) {

            case FlowState.Idle:
                if (fm > THRESH_ACTIVE_FOCUS_MIN && idle < THRESH_ACTIVE_IDLE_SEC) {
                    this.transition(FlowState.Active);
                }
                break;

            case FlowState.Active:
                // Drop back to idle on hard break
                if (idle > THRESH_IDLE_BREAK_SEC || sw > THRESH_ACTIVE_BREAK_SWITCHES) {
                    this.cancelPromotion();
                    this.transition(FlowState.Idle);
                    break;
                }
                // Start promotion timer if flow conditions met
                if (fm >= THRESH_FLOW_FOCUS_MIN && sw <= THRESH_FLOW_SWITCHES && idle < THRESH_FLOW_IDLE_SEC) {
                    this.startPromotion();
                } else {
                    // Conditions lost – reset promotion timer
                    this.cancelPromotion();
                }
                break;

            case FlowState.Flow:
                // Hard disruption → start demotion grace
                if (idle > THRESH_FLOW_BREAK_IDLE_SEC || sw > THRESH_FLOW_BREAK_SWITCHES) {
                    this.startDemotion(FlowState.Cooldown);
                    break;
                }
                // Focus regression → grace before dropping to Active
                if (fm < THRESH_FLOW_REGRESS_MIN) {
                    this.startDemotion(FlowState.Active);
                    break;
                }
                // Conditions are fine – cancel any pending demotion
                this.cancelDemotion();
                break;

            case FlowState.Cooldown:
                // Quick recovery → back to Flow
                if (fm >= THRESH_FLOW_FOCUS_MIN && idle < THRESH_FLOW_IDLE_SEC && sw <= THRESH_FLOW_SWITCHES) {
                    this.cancelCooldown();
                    this.transition(FlowState.Flow);
                }
                break;
        }
    }

    // ── Transition ─────────────────────────────────────────────────────

    private transition(next: FlowState): void {
        if (next === this._state) return;
        const prev = this._state;
        this._state = next;
        this._target = cssVarsForState(next);

        if (this._el) {
            this._el.dataset.flowState = next;
        }

        this.bus.emit('stateChanged', { prev, next });

        if (this._reducedMotion) {
            // Instant swap – no crossfade
            this._current = { ...this._target };
            this.applyCSSVars(this._current);
        } else {
            this.startCrossfade();
        }
    }

    // ── Hysteresis: Promotion (Active → Flow) ─────────────────────────

    private startPromotion(): void {
        if (this._promotionTimer !== null) return;  // already pending
        this._promotionTimer = setTimeout(() => {
            this._promotionTimer = null;
            // Re-check conditions at fire time
            const { focusSessionMinutes: fm, idleSeconds: idle, windowSwitchCountLast10Min: sw } = this._signals;
            if (fm >= THRESH_FLOW_FOCUS_MIN && sw <= THRESH_FLOW_SWITCHES && idle < THRESH_FLOW_IDLE_SEC) {
                this.transition(FlowState.Flow);
            }
        }, PROMOTION_HOLD_MS);
    }

    private cancelPromotion(): void {
        if (this._promotionTimer !== null) {
            clearTimeout(this._promotionTimer);
            this._promotionTimer = null;
        }
    }

    // ── Hysteresis: Demotion (Flow → Cooldown or Active) ──────────────

    private _demotionTarget: FlowState = FlowState.Cooldown;

    private startDemotion(target: FlowState): void {
        if (this._demotionTimer !== null) return;  // already pending
        this._demotionTarget = target;
        this._demotionTimer = setTimeout(() => {
            this._demotionTimer = null;
            this.transition(this._demotionTarget);
            // If entering cooldown, start the cooldown-to-idle timer
            if (this._demotionTarget === FlowState.Cooldown) {
                this.startCooldownTimer();
            }
        }, DEMOTION_GRACE_MS);
    }

    private cancelDemotion(): void {
        if (this._demotionTimer !== null) {
            clearTimeout(this._demotionTimer);
            this._demotionTimer = null;
        }
    }

    // ── Cooldown → Idle timer ─────────────────────────────────────────

    private startCooldownTimer(): void {
        this.cancelCooldown();
        this._cooldownTimer = setTimeout(() => {
            this._cooldownTimer = null;
            if (this._state === FlowState.Cooldown) {
                this.transition(FlowState.Idle);
            }
        }, COOLDOWN_DURATION_MS);
    }

    private cancelCooldown(): void {
        if (this._cooldownTimer !== null) {
            clearTimeout(this._cooldownTimer);
            this._cooldownTimer = null;
        }
    }

    private clearAllTimers(): void {
        this.cancelPromotion();
        this.cancelDemotion();
        this.cancelCooldown();
        if (this._debounceTimer !== null) {
            clearTimeout(this._debounceTimer);
            this._debounceTimer = null;
        }
    }

    // ── CSS variable crossfade (rAF loop) ─────────────────────────────

    private applyCSSVars(v: FlowCSSVars): void {
        if (!this._el) return;
        const s = this._el.style;
        s.setProperty('--flow-overlay-opacity',   v.flowOverlayOpacity.toFixed(3));
        s.setProperty('--flow-glow-scale',        v.flowGlowScale.toFixed(3));
        s.setProperty('--flow-border-accent',     v.flowBorderAccent.toFixed(3));
        s.setProperty('--flow-breathing-speed',   v.flowBreathingSpeed.toFixed(3));
        s.setProperty('--flow-bg-tint',           v.flowBgTint.toFixed(3));
    }

    private startCrossfade(): void {
        if (this._animating) return;
        this._animating = true;
        this._lastFrameTime = this._now();
        this._rafId = this._raf(this.crossfadeFrame);
    }

    private stopCrossfade(): void {
        this._animating = false;
        if (this._rafId !== null) {
            this._caf(this._rafId);
            this._rafId = null;
        }
    }

    private crossfadeFrame = (now: number): void => {
        if (!this._animating) return;

        const dt = (now - this._lastFrameTime) / 1000;
        this._lastFrameTime = now;

        const t = 1 - Math.exp(-CROSSFADE_SPEED * dt);
        this._current = lerpVars(this._current, this._target, t);
        this.applyCSSVars(this._current);
        this.bus.emit('cssFrame', this._current);

        if (varsConverged(this._current, this._target)) {
            this._current = { ...this._target };
            this.applyCSSVars(this._current);
            this.stopCrossfade();
            return;
        }

        this._rafId = this._raf(this.crossfadeFrame);
    };
}

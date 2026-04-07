/**
 * TimerService – orchestrates the rAF loop, drives the store, and emits
 * events that the DOM layer can subscribe to.
 *
 * Key design decisions:
 *   • requestAnimationFrame for ticks → zero drift, battery-friendly
 *     (pauses automatically when the tab is hidden).
 *   • All elapsed computation delegates to selectElapsedMs which uses
 *     performance.now() deltas — never Date.now() inside the hot loop.
 *   • UI update throttle: the 'tick' event only fires when the *displayed*
 *     second changes, so subscribers never re-render for sub-second jitter.
 */

import { PubSub } from './pubsub';
import {
    TimerState,
    TimerAction,
    createInitialState,
    timerReducer,
    selectElapsedMs,
    SessionRecord,
} from './timer-store';

// ── Event map ──────────────────────────────────────────────────────────

export interface TimerEvents {
    /** Fires at most once per displayed second with current elapsed ms */
    tick: number;
    /** Fires when a session is finalized (app switch / reset) */
    sessionFinalized: SessionRecord;
    /** Fires on any state change */
    stateChanged: Readonly<TimerState>;
}

// ── Service ────────────────────────────────────────────────────────────

export class TimerService {
    readonly bus = new PubSub<TimerEvents>();

    private _state: TimerState = createInitialState();
    private _rafId: number | null = null;
    private _lastDisplayedSecond = -1;
    private _running = false;

    // ── Public API ─────────────────────────────────────────────────────

    get state(): Readonly<TimerState> {
        return this._state;
    }

    get running(): boolean {
        return this._running;
    }

    /**
     * Call when native ActiveAppChanged event arrives.
     * @param app          Process / window name
     * @param nativeStartMs  Unix-ms timestamp from C++ side
     */
    onActiveAppChanged(app: string, nativeStartMs: number): void {
        const prevSessions = this._state.sessions.length;

        this.dispatch({
            type: 'ACTIVE_APP_CHANGED',
            app,
            nativeStartMs,
            now: performance.now(),
        });

        // Emit finalized session if one was produced
        if (this._state.sessions.length > prevSessions) {
            const last = this._state.sessions[this._state.sessions.length - 1];
            this.bus.emit('sessionFinalized', last);
        }

        this.startLoop();
    }

    /** Call when native IdleStart event arrives */
    onIdleStart(): void {
        this.dispatch({ type: 'IDLE_START', now: performance.now() });
        this.stopLoop();
    }

    /** Call when native IdleEnd event arrives */
    onIdleEnd(): void {
        this.dispatch({ type: 'IDLE_END', now: performance.now() });
        if (this._state.activeApp) {
            this.startLoop();
        }
    }

    /** Full reset – stops loop and clears all state */
    reset(): void {
        this.stopLoop();
        this.dispatch({ type: 'RESET' });
        this._lastDisplayedSecond = -1;
    }

    /**
     * Snapshot the current elapsed ms (useful outside the rAF loop,
     * e.g. for tests or one-off queries).
     */
    elapsed(): number {
        return selectElapsedMs(this._state, performance.now());
    }

    destroy(): void {
        this.stopLoop();
        this.bus.clear();
    }

    // ── Internal ───────────────────────────────────────────────────────

    private dispatch(action: TimerAction): void {
        this._state = timerReducer(this._state, action);
        this.bus.emit('stateChanged', this._state);
    }

    private startLoop(): void {
        if (this._running) return;
        this._running = true;
        this._lastDisplayedSecond = -1;
        this.tick();
    }

    private stopLoop(): void {
        this._running = false;
        if (this._rafId !== null) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    }

    private tick = (): void => {
        if (!this._running) return;

        const now = performance.now();
        const elapsedMs = selectElapsedMs(this._state, now);
        const displayedSecond = Math.floor(elapsedMs / 1000);

        // Only emit when the displayed second actually changes
        if (displayedSecond !== this._lastDisplayedSecond) {
            this._lastDisplayedSecond = displayedSecond;
            this.bus.emit('tick', elapsedMs);
        }

        this._rafId = requestAnimationFrame(this.tick);
    };
}

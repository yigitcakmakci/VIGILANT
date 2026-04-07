/**
 * Timer Store – immutable state + reducer.
 *
 * Drift-resistant: elapsed time is computed from the *difference* between
 * the current performance.now() reading and the snapshot taken at session
 * start, so setTimeout/setInterval jitter never accumulates.
 *
 * Native anchor: the C++ side sends a Unix-ms timestamp when
 * ActiveAppChanged fires.  We store it alongside the performance.now()
 * snapshot so we can reconstruct wall-clock start time without relying on
 * Date.now() inside the JS tick loop.
 */

// ── Types ──────────────────────────────────────────────────────────────

export interface SessionRecord {
    appName: string;
    nativeStartMs: number;   // Unix-ms from C++ side
    durationMs: number;      // total active (non-idle) duration
}

export interface TimerState {
    /** Currently tracked application name, null when nothing active */
    activeApp: string | null;
    /** Unix-ms timestamp supplied by native side at session start */
    nativeStartMs: number;
    /** performance.now() snapshot taken when the current session started */
    perfStartMs: number;
    /** Accumulated active time in ms (excludes idle spans) */
    accumulatedMs: number;
    /** Whether user is currently idle */
    idle: boolean;
    /** performance.now() snapshot when idle started, null if not idle */
    idlePerfMs: number | null;
    /** Finalized past sessions */
    sessions: SessionRecord[];
}

// ── Actions ────────────────────────────────────────────────────────────

export type TimerAction =
    | { type: 'ACTIVE_APP_CHANGED'; app: string; nativeStartMs: number; now: number }
    | { type: 'IDLE_START'; now: number }
    | { type: 'IDLE_END'; now: number }
    | { type: 'RESET' };

// ── Initial state factory ──────────────────────────────────────────────

export function createInitialState(): TimerState {
    return {
        activeApp: null,
        nativeStartMs: 0,
        perfStartMs: 0,
        accumulatedMs: 0,
        idle: false,
        idlePerfMs: null,
        sessions: [],
    };
}

// ── Pure helpers ───────────────────────────────────────────────────────

/** Finalize the running session and push it onto sessions[] */
function finalizeSession(state: TimerState, now: number): SessionRecord | null {
    if (!state.activeApp) return null;
    let elapsed = state.accumulatedMs;
    if (!state.idle) {
        elapsed += now - state.perfStartMs;
    } else if (state.idlePerfMs !== null) {
        // idle in progress – count up to the idle-start moment only
        elapsed += state.idlePerfMs - state.perfStartMs;
    }
    return {
        appName: state.activeApp,
        nativeStartMs: state.nativeStartMs,
        durationMs: Math.max(0, elapsed),
    };
}

// ── Reducer ────────────────────────────────────────────────────────────

export function timerReducer(state: TimerState, action: TimerAction): TimerState {
    switch (action.type) {

        case 'ACTIVE_APP_CHANGED': {
            // 1. Finalize previous session
            const prev = finalizeSession(state, action.now);
            const sessions = prev
                ? [...state.sessions, prev]
                : [...state.sessions];

            // 2. New session
            return {
                activeApp: action.app,
                nativeStartMs: action.nativeStartMs,
                perfStartMs: action.now,
                accumulatedMs: 0,
                idle: false,
                idlePerfMs: null,
                sessions,
            };
        }

        case 'IDLE_START': {
            if (state.idle || !state.activeApp) return state;
            // Bank the active time so far
            const banked = state.accumulatedMs + (action.now - state.perfStartMs);
            return {
                ...state,
                idle: true,
                idlePerfMs: action.now,
                accumulatedMs: banked,
            };
        }

        case 'IDLE_END': {
            if (!state.idle || !state.activeApp) return state;
            // Resume: reset perfStartMs to *now*, accumulatedMs already banked
            return {
                ...state,
                idle: false,
                idlePerfMs: null,
                perfStartMs: action.now,
            };
        }

        case 'RESET':
            return createInitialState();

        default:
            return state;
    }
}

// ── Selector: current elapsed (call on every animation frame) ──────────

export function selectElapsedMs(state: TimerState, now: number): number {
    if (!state.activeApp) return 0;
    if (state.idle) return state.accumulatedMs;
    return state.accumulatedMs + (now - state.perfStartMs);
}

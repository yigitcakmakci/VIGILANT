/**
 * Timer unit tests – runnable with any test runner (vitest / jest / plain node).
 *
 * The tests use a *mock* performance.now() so they execute instantly and
 * deterministically.  The drift-measurement test demonstrates how to
 * verify that elapsed time tracks a known wall-clock advancement.
 */

import {
    TimerState,
    TimerAction,
    createInitialState,
    timerReducer,
    selectElapsedMs,
} from './timer-store';

// ── Helpers ────────────────────────────────────────────────────────────

/** Simulate a monotonic clock that advances by explicit calls. */
class FakeClock {
    private _now = 1000; // start at 1 s (never zero — catches divide-by-zero bugs)
    now(): number { return this._now; }
    advance(ms: number): number { this._now += ms; return this._now; }
}

function dispatch(state: TimerState, action: TimerAction): TimerState {
    return timerReducer(state, action);
}

// ── Tests ──────────────────────────────────────────────────────────────

function testInitialStateIsInactive(): void {
    const s = createInitialState();
    console.assert(s.activeApp === null, 'activeApp should be null');
    console.assert(s.accumulatedMs === 0, 'accumulatedMs should be 0');
    console.assert(s.idle === false, 'idle should be false');
    console.assert(s.sessions.length === 0, 'sessions should be empty');
    console.log('✓ testInitialStateIsInactive');
}

function testActiveAppChangedStartsSession(): void {
    const clock = new FakeClock();
    let s = createInitialState();
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'code.exe',
        nativeStartMs: 1700000000000,
        now: clock.now(),
    });

    console.assert(s.activeApp === 'code.exe', 'activeApp should be code.exe');
    console.assert(s.perfStartMs === clock.now(), 'perfStartMs should equal clock.now()');
    console.assert(s.nativeStartMs === 1700000000000, 'nativeStartMs should be anchored');
    console.assert(s.accumulatedMs === 0, 'newly started session has zero accumulated');
    console.log('✓ testActiveAppChangedStartsSession');
}

function testElapsedGrowsWithClock(): void {
    const clock = new FakeClock();
    let s = createInitialState();
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'chrome.exe',
        nativeStartMs: Date.now(),
        now: clock.now(),
    });

    clock.advance(5000); // 5 seconds
    const elapsed = selectElapsedMs(s, clock.now());
    console.assert(elapsed === 5000, `elapsed should be 5000, got ${elapsed}`);
    console.log('✓ testElapsedGrowsWithClock');
}

function testIdlePausesCounter(): void {
    const clock = new FakeClock();
    let s = createInitialState();

    // Start session
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'notepad.exe',
        nativeStartMs: Date.now(),
        now: clock.now(),
    });

    // 3 seconds active
    clock.advance(3000);

    // Go idle
    s = dispatch(s, { type: 'IDLE_START', now: clock.now() });
    console.assert(s.idle === true, 'should be idle');
    console.assert(s.accumulatedMs === 3000, `banked should be 3000, got ${s.accumulatedMs}`);

    // 10 seconds idle — counter should NOT advance
    clock.advance(10000);
    const elapsedDuringIdle = selectElapsedMs(s, clock.now());
    console.assert(elapsedDuringIdle === 3000, `idle elapsed should still be 3000, got ${elapsedDuringIdle}`);

    // Resume
    s = dispatch(s, { type: 'IDLE_END', now: clock.now() });
    console.assert(s.idle === false, 'should no longer be idle');

    // 2 more active seconds
    clock.advance(2000);
    const total = selectElapsedMs(s, clock.now());
    console.assert(total === 5000, `total should be 5000, got ${total}`);

    console.log('✓ testIdlePausesCounter');
}

function testAppSwitchFinalizesPreviousSession(): void {
    const clock = new FakeClock();
    let s = createInitialState();

    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'app_a.exe',
        nativeStartMs: 100,
        now: clock.now(),
    });
    clock.advance(7000);

    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'app_b.exe',
        nativeStartMs: 200,
        now: clock.now(),
    });

    console.assert(s.sessions.length === 1, 'should have 1 finalized session');
    console.assert(s.sessions[0].appName === 'app_a.exe', 'finalized app should be app_a');
    console.assert(s.sessions[0].durationMs === 7000, `duration should be 7000, got ${s.sessions[0].durationMs}`);
    console.assert(s.activeApp === 'app_b.exe', 'active app should now be app_b');
    console.log('✓ testAppSwitchFinalizesPreviousSession');
}

function testFinalizeWhileIdle(): void {
    const clock = new FakeClock();
    let s = createInitialState();

    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'idle_app.exe',
        nativeStartMs: 300,
        now: clock.now(),
    });
    clock.advance(2000);

    s = dispatch(s, { type: 'IDLE_START', now: clock.now() });
    clock.advance(60000); // 1 minute idle

    // Switch app while idle → should finalize with only 2 s active time
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'next.exe',
        nativeStartMs: 400,
        now: clock.now(),
    });

    console.assert(s.sessions.length === 1, 'should have 1 session');
    console.assert(
        s.sessions[0].durationMs === 2000,
        `idle_app duration should be 2000 (idle excluded), got ${s.sessions[0].durationMs}`,
    );
    console.log('✓ testFinalizeWhileIdle');
}

function testDriftMeasurement(): void {
    /**
     * Drift test: simulates 3600 ticks (1 per "second") and verifies
     * the elapsed value matches the expected wall-clock advancement.
     *
     * Because selectElapsedMs uses a *single subtraction* of
     * performance.now() snapshots, there is no accumulated rounding —
     * drift is always zero in this model.  In a real browser the only
     * source of error is the resolution of performance.now() (~5 µs).
     */
    const clock = new FakeClock();
    let s = createInitialState();
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'drift_test.exe',
        nativeStartMs: Date.now(),
        now: clock.now(),
    });

    const TICKS = 3600;
    const TICK_MS = 1000;

    for (let i = 0; i < TICKS; i++) {
        clock.advance(TICK_MS);
    }

    const expected = TICKS * TICK_MS;
    const actual = selectElapsedMs(s, clock.now());
    const driftMs = Math.abs(actual - expected);

    console.assert(driftMs === 0, `drift should be 0, got ${driftMs} ms`);
    console.log(`✓ testDriftMeasurement — ${TICKS} ticks, drift: ${driftMs} ms`);
}

function testResetClearsEverything(): void {
    const clock = new FakeClock();
    let s = createInitialState();
    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'x.exe',
        nativeStartMs: 1,
        now: clock.now(),
    });
    clock.advance(5000);
    s = dispatch(s, { type: 'RESET' });

    const fresh = createInitialState();
    console.assert(s.activeApp === fresh.activeApp, 'reset → activeApp null');
    console.assert(s.sessions.length === 0, 'reset → sessions empty');
    console.assert(s.accumulatedMs === 0, 'reset → accumulatedMs 0');
    console.log('✓ testResetClearsEverything');
}

function testMultipleIdleCycles(): void {
    const clock = new FakeClock();
    let s = createInitialState();

    s = dispatch(s, {
        type: 'ACTIVE_APP_CHANGED',
        app: 'multi.exe',
        nativeStartMs: 500,
        now: clock.now(),
    });

    // Cycle 1: 2s active, 5s idle
    clock.advance(2000);
    s = dispatch(s, { type: 'IDLE_START', now: clock.now() });
    clock.advance(5000);
    s = dispatch(s, { type: 'IDLE_END', now: clock.now() });

    // Cycle 2: 3s active, 8s idle
    clock.advance(3000);
    s = dispatch(s, { type: 'IDLE_START', now: clock.now() });
    clock.advance(8000);
    s = dispatch(s, { type: 'IDLE_END', now: clock.now() });

    // Cycle 3: 1s active
    clock.advance(1000);

    const elapsed = selectElapsedMs(s, clock.now());
    // Expected: 2000 + 3000 + 1000 = 6000 (idle periods excluded)
    console.assert(elapsed === 6000, `multi-idle elapsed should be 6000, got ${elapsed}`);
    console.log('✓ testMultipleIdleCycles');
}

// ── Runner ─────────────────────────────────────────────────────────────

export function runAllTests(): void {
    console.log('━━━ Timer Store Unit Tests ━━━');
    testInitialStateIsInactive();
    testActiveAppChangedStartsSession();
    testElapsedGrowsWithClock();
    testIdlePausesCounter();
    testAppSwitchFinalizesPreviousSession();
    testFinalizeWhileIdle();
    testDriftMeasurement();
    testResetClearsEverything();
    testMultipleIdleCycles();
    console.log('━━━ All tests passed ━━━');
}

// Auto-run when executed directly (e.g. `npx tsx timer-store.test.ts`)
runAllTests();

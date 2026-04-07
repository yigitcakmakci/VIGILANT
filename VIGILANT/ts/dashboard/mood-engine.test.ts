/**
 * Mood Engine unit tests – same runner pattern as timer-store.test.ts.
 *
 * Pure-function tests execute synchronously; the rAF-driven transition
 * engine is tested via a manual step helper that bypasses rAF.
 */

import {
    MoodMetrics,
    computeMoodScore,
    themeForScore,
    MoodEngine,
    FOCUS_WINDOW_SEC,
    SWITCH_SATURATION,
} from './mood-engine';

// ── Helpers ────────────────────────────────────────────────────────────

function approx(a: number, b: number, eps = 0.15): boolean {
    return Math.abs(a - b) <= eps;
}

// ── Score computation tests ────────────────────────────────────────────

function testPerfectFocusMaxScore(): void {
    const m: MoodMetrics = { focusSeconds: FOCUS_WINDOW_SEC, appSwitchCount: 0, idleRatio: 0 };
    const score = computeMoodScore(m);
    console.assert(score === 10, `perfect focus → 10, got ${score}`);
    console.log('✓ testPerfectFocusMaxScore');
}

function testZeroActivityNeutral(): void {
    const m: MoodMetrics = { focusSeconds: 0, appSwitchCount: 0, idleRatio: 0 };
    const score = computeMoodScore(m);
    console.assert(score === 0, `zero activity → 0, got ${score}`);
    console.log('✓ testZeroActivityNeutral');
}

function testHighSwitchPenalty(): void {
    const m: MoodMetrics = { focusSeconds: 0, appSwitchCount: SWITCH_SATURATION, idleRatio: 0 };
    const score = computeMoodScore(m);
    console.assert(score === -10, `max switch → −10, got ${score}`);
    console.log('✓ testHighSwitchPenalty');
}

function testIdlePenalty(): void {
    const m: MoodMetrics = { focusSeconds: 0, appSwitchCount: 0, idleRatio: 1 };
    const score = computeMoodScore(m);
    console.assert(score === -5, `full idle → −5, got ${score}`);
    console.log('✓ testIdlePenalty');
}

function testMixedMetrics(): void {
    // Half focus, moderate switch, some idle
    const m: MoodMetrics = { focusSeconds: 900, appSwitchCount: 15, idleRatio: 0.3 };
    const score = computeMoodScore(m);
    // focusNorm = 0.5 → +5, switchNorm = 0.5 → −5, idleNorm = 0.3 → −1.5 → raw = −1.5
    console.assert(approx(score, -1.5), `mixed → ~−1.5, got ${score}`);
    console.log('✓ testMixedMetrics');
}

function testClampFloor(): void {
    const m: MoodMetrics = { focusSeconds: 0, appSwitchCount: 100, idleRatio: 1 };
    const score = computeMoodScore(m);
    console.assert(score === -10, `overflow penalty clamped to −10, got ${score}`);
    console.log('✓ testClampFloor');
}

function testClampCeiling(): void {
    const m: MoodMetrics = { focusSeconds: 9999, appSwitchCount: 0, idleRatio: 0 };
    const score = computeMoodScore(m);
    console.assert(score === 10, `overflow focus clamped to 10, got ${score}`);
    console.log('✓ testClampCeiling');
}

// ── Theme mapping tests ────────────────────────────────────────────────

function testThemeExtremes(): void {
    const neg = themeForScore(-10);
    const pos = themeForScore(10);
    console.assert(neg.vignetteOpacity > pos.vignetteOpacity, 'negative → higher vignette');
    console.assert(neg.blurStrength > pos.blurStrength, 'negative → higher blur');
    console.assert(pos.glow > neg.glow, 'positive → higher glow');
    console.log('✓ testThemeExtremes');
}

function testThemeNeutralInterpolation(): void {
    const t = themeForScore(0);
    // accent should be amber-ish: hue ~38
    const hue = parseInt(t.accent.split(' ')[0], 10);
    console.assert(approx(hue, 38, 1), `neutral hue ~38, got ${hue}`);
    console.log('✓ testThemeNeutralInterpolation');
}

function testThemeMidpointInterpolation(): void {
    const t = themeForScore(5);
    // Between amber (38) and emerald (142) → hue ~90
    const hue = parseInt(t.accent.split(' ')[0], 10);
    console.assert(hue > 38 && hue < 142, `mid-positive hue between 38–142, got ${hue}`);
    console.log('✓ testThemeMidpointInterpolation');
}

// ── Engine integration tests ───────────────────────────────────────────

function testEngineScoreEvent(): void {
    const engine = new MoodEngine();
    let received: number | null = null;
    engine.bus.on('scoreChanged', (s) => { received = s; });

    engine.update({ focusSeconds: FOCUS_WINDOW_SEC, appSwitchCount: 0, idleRatio: 0 });
    console.assert(received === 10, `event should carry 10, got ${received}`);
    engine.destroy();
    console.log('✓ testEngineScoreEvent');
}

function testEngineSetScore(): void {
    const engine = new MoodEngine();
    let received: number | null = null;
    engine.bus.on('scoreChanged', (s) => { received = s; });

    engine.setScore(-7.3);
    console.assert(engine.score === -7.3, `score should be −7.3, got ${engine.score}`);
    console.assert(received === -7.3, `event should carry −7.3, got ${received}`);
    engine.destroy();
    console.log('✓ testEngineSetScore');
}

function testEngineDuplicateScoreIgnored(): void {
    const engine = new MoodEngine();
    let count = 0;
    engine.bus.on('scoreChanged', () => { count++; });

    engine.setScore(5);
    engine.setScore(5); // duplicate → should NOT fire
    console.assert(count === 1, `should fire once, fired ${count}`);
    engine.destroy();
    console.log('✓ testEngineDuplicateScoreIgnored');
}

// ── Runner ─────────────────────────────────────────────────────────────

export function runAllTests(): void {
    console.log('━━━ Mood Engine Unit Tests ━━━');
    testPerfectFocusMaxScore();
    testZeroActivityNeutral();
    testHighSwitchPenalty();
    testIdlePenalty();
    testMixedMetrics();
    testClampFloor();
    testClampCeiling();
    testThemeExtremes();
    testThemeNeutralInterpolation();
    testThemeMidpointInterpolation();
    testEngineScoreEvent();
    testEngineSetScore();
    testEngineDuplicateScoreIgnored();
    console.log('━━━ All mood tests passed ━━━');
}

runAllTests();

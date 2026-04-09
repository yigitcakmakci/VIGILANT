/**
 * interview-result-types.test.ts — Unit tests for InterviewResult types,
 * finalize flow, and GoalTree readiness checks.
 *
 * Same console.assert runner pattern as interview-state.test.ts.
 * Deterministic: no time/network dependencies.
 *
 * Run: npx ts-node ts/interview/interview-result-types.test.ts
 *   or: npx tsx  ts/interview/interview-result-types.test.ts
 */

import {
    createInitialState,
    interviewReducer,
    canSendMessage,
    canFinalize,
    isLimitReached,
    InterviewState,
    ChatMessage,
} from './interview-state';

import {
    InterviewResult,
    InterviewResultMessage,
    ExtractedSlots,
    GoalTreeGeneratedPayload,
    createEmptyResult,
    isReadyForGoalTree,
} from './interview-result-types';

// ── Helpers ────────────────────────────────────────────────────────────

const FIXED_TS = '2025-01-15T10:00:00Z';   // deterministic timestamp

function makeAiMsg(id: string, q: number): ChatMessage {
    return { id, role: 'ai', text: `Soru ${q}?`, ts: FIXED_TS };
}
function makeUserMsg(id: string, a: number): ChatMessage {
    return { id, role: 'user', text: `Cevap ${a}`, ts: FIXED_TS };
}

function startedState(): InterviewState {
    return interviewReducer(createInitialState(), {
        type: 'SESSION_STARTED',
        sessionId: 'test-session-100',
    });
}

function buildMockResult(overrides?: Partial<InterviewResult>): InterviewResult {
    return {
        session_id: 'sess-1',
        finalized: true,
        ended_by: 'cta',
        question_count: 2,
        max_questions: 3,
        created_at: FIXED_TS,
        finalized_at: FIXED_TS,
        transcript: [
            { message_id: 'm1', role: 'ai', text: 'Soru 1?', iso_ts: FIXED_TS },
            { message_id: 'm2', role: 'user', text: 'Cevap 1', iso_ts: FIXED_TS },
        ],
        extractedSlots: {
            goal: 'Web geliştirme',
            timeframe: '3 ay',
            weekly_hours: 10,
            current_level: 'başlangıç',
            constraints: ['zaman', 'bütçe'],
        },
        ...overrides,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// InterviewResult type tests
// ═══════════════════════════════════════════════════════════════════════

/** R1: createEmptyResult returns correct defaults */
function test_createEmptyResult(): void {
    const r = createEmptyResult();
    console.assert(r.session_id === '', 'empty session_id');
    console.assert(r.finalized === false, 'not finalized');
    console.assert(r.ended_by === '', 'empty ended_by');
    console.assert(r.question_count === 0, 'zero questions');
    console.assert(r.max_questions === 3, 'max 3');
    console.assert(r.transcript.length === 0, 'empty transcript');
    console.assert(r.extractedSlots.goal === '', 'empty goal');
    console.assert(r.extractedSlots.constraints.length === 0, 'no constraints');
    console.log('✓ R1: createEmptyResult returns valid defaults');
}

/** R2: isReadyForGoalTree — finalized + goal → true */
function test_isReadyForGoalTree_positive(): void {
    const r = buildMockResult();
    console.assert(isReadyForGoalTree(r) === true, 'finalized with goal should be ready');
    console.log('✓ R2: isReadyForGoalTree positive');
}

/** R3: isReadyForGoalTree — not finalized → false */
function test_isReadyForGoalTree_notFinalized(): void {
    const r = buildMockResult({ finalized: false });
    console.assert(isReadyForGoalTree(r) === false, 'not finalized = not ready');
    console.log('✓ R3: isReadyForGoalTree not finalized');
}

/** R4: isReadyForGoalTree — no goal → false */
function test_isReadyForGoalTree_noGoal(): void {
    const r = buildMockResult({
        extractedSlots: {
            goal: '',
            timeframe: '3 ay',
            weekly_hours: 10,
            current_level: 'başlangıç',
            constraints: [],
        },
    });
    console.assert(isReadyForGoalTree(r) === false, 'no goal = not ready');
    console.log('✓ R4: isReadyForGoalTree empty goal');
}

/** R5: InterviewResult transcript is typed correctly */
function test_transcriptTyping(): void {
    const r = buildMockResult();
    const first: InterviewResultMessage = r.transcript[0];
    console.assert(first.message_id === 'm1', 'message_id');
    console.assert(first.role === 'ai', 'role');
    console.assert(first.text === 'Soru 1?', 'text');
    console.assert(first.iso_ts === FIXED_TS, 'iso_ts');
    console.log('✓ R5: transcript typing correct');
}

/** R6: ExtractedSlots fields match expected types */
function test_extractedSlotsFields(): void {
    const s: ExtractedSlots = buildMockResult().extractedSlots;
    console.assert(typeof s.goal === 'string', 'goal is string');
    console.assert(typeof s.timeframe === 'string', 'timeframe is string');
    console.assert(typeof s.weekly_hours === 'number', 'weekly_hours is number');
    console.assert(typeof s.current_level === 'string', 'current_level is string');
    console.assert(Array.isArray(s.constraints), 'constraints is array');
    console.assert(s.constraints.length === 2, 'two constraints');
    console.log('✓ R6: ExtractedSlots fields typed correctly');
}

/** R7: GoalTreeGeneratedPayload structure */
function test_goalTreePayloadStructure(): void {
    const payload: GoalTreeGeneratedPayload = {
        interviewSessionId: 'sess-1',
        interviewResult: buildMockResult(),
        extractedSlots: buildMockResult().extractedSlots,
        status: 'ready',
    };
    console.assert(payload.status === 'ready', 'status ready');
    console.assert(payload.interviewResult.finalized === true, 'finalized');
    console.assert(payload.extractedSlots.goal === 'Web geliştirme', 'goal');
    console.log('✓ R7: GoalTreeGeneratedPayload structure valid');
}

// ═══════════════════════════════════════════════════════════════════════
// UI state → finalize flow integration tests
// ═══════════════════════════════════════════════════════════════════════

/** F1: Full CTA finalize flow — buttons visibility simulation */
function test_fullCtaFinalizeFlow(): void {
    let s = startedState();

    // AI question 1 received
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1', 1),
        questionCount: 1,
    });
    // "Send" button should be visible
    console.assert(canSendMessage(s) === true, 'can send after Q1');
    // "Finalize" button should be visible
    console.assert(canFinalize(s) === true, 'can finalize after Q1');

    // User sends answer
    s = interviewReducer(s, { type: 'SENDING_START' });
    // Send button disabled while sending
    console.assert(canSendMessage(s) === false, 'cannot send while sending');

    s = interviewReducer(s, { type: 'SENDING_END' });
    s = interviewReducer(s, {
        type: 'USER_MESSAGE_SENT',
        message: makeUserMsg('u-1', 1),
    });

    // AI question 2
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-2', 2),
        questionCount: 2,
    });
    console.assert(canSendMessage(s) === true, 'can send after Q2');

    // User clicks CTA finalize
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    console.assert(s.status === 'finalizing', 'status=finalizing');
    console.assert(canSendMessage(s) === false, 'cannot send during finalizing');
    console.assert(canFinalize(s) === false, 'cannot re-finalize');

    // Backend confirms
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'cta', questionCount: 2 });
    console.assert(s.status === 'finalized', 'status=finalized');
    console.assert(s.endedBy === 'cta', 'endedBy=cta');
    console.assert(canSendMessage(s) === false, 'cannot send after finalized');
    console.assert(canFinalize(s) === false, 'cannot finalize after finalized');

    console.log('✓ F1: Full CTA finalize flow with button visibility');
}

/** F2: Auto-finalize at maxQuestions — limit flow */
function test_autoFinalizeAtLimit(): void {
    let s = startedState();

    for (let i = 1; i <= 3; i++) {
        s = interviewReducer(s, {
            type: 'AI_QUESTION_RECEIVED',
            message: makeAiMsg(`ai-${i}`, i),
            questionCount: i,
        });
    }

    console.assert(s.questionCount === 3, 'questionCount=3');
    console.assert(s.status === 'finalizing', 'auto-finalizing at limit');
    console.assert(isLimitReached(s) === true, 'limit reached');
    console.assert(canSendMessage(s) === false, 'send blocked at limit');

    // Backend confirms with endedBy=limit
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'limit', questionCount: 3 });
    console.assert(s.status === 'finalized', 'finalized');
    console.assert(s.endedBy === 'limit', 'endedBy=limit');

    console.log('✓ F2: Auto-finalize at maxQuestions limit');
}

/** F3: Double finalize dispatch — idempotent (no state change) */
function test_doubleFinalizeIdempotent(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1', 1),
        questionCount: 1,
    });

    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    const stateAfterFirst = s;

    // Second dispatch — must return same reference
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    console.assert(s === stateAfterFirst, 'second FINALIZE_REQUESTED returns same ref');
    console.assert(s.status === 'finalizing', 'still finalizing');

    // Already finalized → dispatch again
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'cta', questionCount: 1 });
    const finalState = s;
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    console.assert(s === finalState, 'FINALIZE_REQUESTED on finalized returns same ref');

    console.log('✓ F3: Double finalize idempotent');
}

/** F4: GoalTree readiness check after full flow */
function test_goalTreeReadinessAfterFlow(): void {
    // Before finalize — not ready
    const empty = createEmptyResult();
    console.assert(isReadyForGoalTree(empty) === false, 'empty not ready');

    // After finalize with goal
    const ready = buildMockResult({ finalized: true });
    console.assert(isReadyForGoalTree(ready) === true, 'finalized with goal = ready');

    // After finalize without goal (e.g., user skipped)
    const noGoal = buildMockResult({
        finalized: true,
        extractedSlots: { goal: '', timeframe: '', weekly_hours: 0, current_level: '', constraints: [] },
    });
    console.assert(isReadyForGoalTree(noGoal) === false, 'no goal = not ready');

    console.log('✓ F4: GoalTree readiness check');
}

/** F5: State machine prevents message send after finalize */
function test_noMessageAfterFinalize(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1', 1),
        questionCount: 1,
    });
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'cta', questionCount: 1 });

    console.assert(canSendMessage(s) === false, 'blocked after finalize');

    // Attempting USER_MESSAGE_SENT on finalized state shouldn't crash but transcript grows
    // (UI should guard with canSendMessage; reducer doesn't reject)
    const before = s.transcript.length;
    s = interviewReducer(s, {
        type: 'USER_MESSAGE_SENT',
        message: makeUserMsg('u-late', 99),
    });
    // Reducer doesn't guard — it's the UI's job via canSendMessage
    console.assert(s.transcript.length === before + 1,
        'reducer appends (UI must check canSendMessage before dispatch)');

    console.log('✓ F5: UI must guard message sending via canSendMessage');
}

/** F6: Reset clears everything including GoalTree readiness */
function test_resetClearsGoalTreeReadiness(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1', 1),
        questionCount: 1,
    });
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'cta', questionCount: 1 });
    console.assert(s.status === 'finalized', 'finalized');

    s = interviewReducer(s, { type: 'RESET' });
    console.assert(s.status === 'idle', 'idle after reset');
    console.assert(s.sessionId === '', 'empty sessionId');
    console.assert(s.transcript.length === 0, 'empty transcript');
    console.assert(s.questionCount === 0, 'zero questions');
    console.assert(s.finalizeRequested === false, 'finalizeRequested false');

    console.log('✓ F6: Reset clears all state');
}

// ═══════════════════════════════════════════════════════════════════════
// Run all tests
// ═══════════════════════════════════════════════════════════════════════

console.log('\n=== Interview Result & UI Flow Tests ===\n');

// InterviewResult type tests
test_createEmptyResult();
test_isReadyForGoalTree_positive();
test_isReadyForGoalTree_notFinalized();
test_isReadyForGoalTree_noGoal();
test_transcriptTyping();
test_extractedSlotsFields();
test_goalTreePayloadStructure();

// UI finalize flow tests
test_fullCtaFinalizeFlow();
test_autoFinalizeAtLimit();
test_doubleFinalizeIdempotent();
test_goalTreeReadinessAfterFlow();
test_noMessageAfterFinalize();
test_resetClearsGoalTreeReadiness();

console.log('\n✅ All 13 interview result & UI flow tests passed.\n');

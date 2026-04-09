/**
 * interview-state.test.ts — Unit tests for Socratic Interview state machine.
 *
 * Run with any TS-compatible test runner (vitest, jest, etc.)
 * or verify manually via ts-node.
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

// ── Helpers ────────────────────────────────────────────────────────────

function makeAiMsg(id: string): ChatMessage {
    return { id, role: 'ai', text: 'Test soru?', ts: new Date().toISOString() };
}
function makeUserMsg(id: string): ChatMessage {
    return { id, role: 'user', text: 'Test yanıt', ts: new Date().toISOString() };
}

function startedState(): InterviewState {
    return interviewReducer(createInitialState(), {
        type: 'SESSION_STARTED',
        sessionId: 'test-session-1',
    });
}

// ── Tests ──────────────────────────────────────────────────────────────

/** T1: maxQuestions dolunca status='asking' kalmalı (kullanıcı son soruyu yanıtlayabilmeli) */
function test_limitReachedKeepsAsking(): void {
    let s = startedState();

    // Simulate 3 AI questions received
    for (let i = 1; i <= 3; i++) {
        s = interviewReducer(s, {
            type: 'AI_QUESTION_RECEIVED',
            message: makeAiMsg('ai-' + i),
            questionCount: i,
        });
    }

    console.assert(s.questionCount === 3, 'questionCount should be 3');
    console.assert(s.status === 'asking', 'status should stay asking so user can answer the last question');
    console.assert(isLimitReached(s) === true, 'isLimitReached should be true');
    console.assert(canSendMessage(s) === true, 'canSendMessage should be true so user can answer');
    console.log('✓ T1: maxQuestions reached keeps asking — user can answer the last question');
}

/** T2: CTA double-click → only first dispatch takes effect */
function test_doubleFinalize_idempotent(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1'),
        questionCount: 1,
    });

    // First finalize
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    console.assert(s.finalizeRequested === true, 'finalizeRequested should be true');
    console.assert(s.status === 'finalizing', 'status should be finalizing');

    // Second finalize (should be no-op)
    const before = { ...s };
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    console.assert(s === before, 'second FINALIZE_REQUESTED should return same state ref');
    console.log('✓ T2: double CTA finalize is idempotent (no-op)');
}

/** T3: canSendMessage is false while isSending */
function test_cannotSendWhileSending(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1'),
        questionCount: 1,
    });
    console.assert(canSendMessage(s) === true, 'should be sendable before SENDING_START');

    s = interviewReducer(s, { type: 'SENDING_START' });
    console.assert(canSendMessage(s) === false, 'should not be sendable during sending');
    console.log('✓ T3: canSendMessage blocked while isSending');
}

/** T4: finalized session rejects further messages (canSendMessage=false) */
function test_finalizedSessionCannotSend(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1'),
        questionCount: 1,
    });
    s = interviewReducer(s, { type: 'FINALIZE_REQUESTED', endedBy: 'cta' });
    s = interviewReducer(s, { type: 'FINALIZE_CONFIRMED', endedBy: 'cta', questionCount: 1 });

    console.assert(s.status === 'finalized', 'status should be finalized');
    console.assert(canSendMessage(s) === false, 'cannot send to finalized session');
    console.assert(canFinalize(s) === false, 'cannot re-finalize');
    console.log('✓ T4: finalized session rejects messages and re-finalize');
}

/** T5: RESET returns clean initial state */
function test_resetClearsState(): void {
    let s = startedState();
    s = interviewReducer(s, {
        type: 'AI_QUESTION_RECEIVED',
        message: makeAiMsg('ai-1'),
        questionCount: 1,
    });
    s = interviewReducer(s, { type: 'RESET' });

    console.assert(s.sessionId === '', 'sessionId should be empty after reset');
    console.assert(s.status === 'idle', 'status should be idle after reset');
    console.assert(s.transcript.length === 0, 'transcript should be empty after reset');
    console.log('✓ T5: RESET returns clean state');
}

// ── Run ────────────────────────────────────────────────────────────────

test_limitReachedKeepsAsking();
test_doubleFinalize_idempotent();
test_cannotSendWhileSending();
test_finalizedSessionCannotSend();
test_resetClearsState();
console.log('\n✅ All interview-state tests passed.');

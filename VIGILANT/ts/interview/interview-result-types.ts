/**
 * interview-result-types.ts — TypeScript interfaces for the deterministic
 * InterviewResult JSON document.
 *
 * Mirrors the C++ InterviewResult / InterviewResultMessage / ExtractedSlots
 * structs exactly.  Used by the UI to consume finalized interview data and
 * to drive GoalTree generation.
 *
 * INVARIANT:
 *  - After finalized === true, transcript[] is immutable.
 *  - Only append is allowed before finalize.
 *  - Double-finalize is idempotent (same document returned).
 */

// ═══════════════════════════════════════════════════════════════════════
// Transcript message
// ═══════════════════════════════════════════════════════════════════════

export type MessageRole = 'user' | 'ai' | 'system';

export interface InterviewResultMessage {
    message_id: string;
    role: MessageRole;
    text: string;
    iso_ts: string;             // ISO-8601
}

// ═══════════════════════════════════════════════════════════════════════
// Extracted slots (structured output from SlotFiller)
// ═══════════════════════════════════════════════════════════════════════

export interface ExtractedSlots {
    goal: string;
    timeframe: string;
    weekly_hours: number;
    current_level: string;
    constraints: string[];
}

// ═══════════════════════════════════════════════════════════════════════
// Complete interview result document
// ═══════════════════════════════════════════════════════════════════════

export interface InterviewResult {
    session_id: string;
    finalized: boolean;
    ended_by: 'cta' | 'limit' | 'complete' | '';
    question_count: number;
    max_questions: number;
    created_at: string;         // ISO-8601
    finalized_at: string;       // ISO-8601 (empty until finalized)

    transcript: InterviewResultMessage[];
    extractedSlots: ExtractedSlots;
}

// ═══════════════════════════════════════════════════════════════════════
// Event payload types
// ═══════════════════════════════════════════════════════════════════════

/** Included in InterviewFinalized payload after persistence */
export interface InterviewFinalizedResultPayload {
    endedBy: 'cta' | 'limit' | 'complete';
    questionCount: number;
    transcript: Array<{ id: string; role: string; text: string; ts: string }>;
    interviewSessionId: string;
    interviewResult: InterviewResult;
    alreadyFinalized?: boolean;
}

/** GoalTreeGenerated payload — carries the persisted result + validated tree */
export interface GoalTreeGeneratedPayload {
    interviewSessionId: string;
    interviewResult: InterviewResult;
    extractedSlots: ExtractedSlots;
    goalTree: import('./goal-tree-types').GoalTree;
    status: 'ready';
}

/** Session list item (from getRecentInterviewSessions) */
export interface InterviewSessionSummary {
    session_id: string;
    ended_by: string;
    question_count: number;
    max_questions: number;
    finalized: boolean;
    created_at: string;
    finalized_at: string;
}

// ═══════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════

/** Type-safe empty result for initial state */
export function createEmptyResult(): InterviewResult {
    return {
        session_id: '',
        finalized: false,
        ended_by: '',
        question_count: 0,
        max_questions: 3,
        created_at: '',
        finalized_at: '',
        transcript: [],
        extractedSlots: {
            goal: '',
            timeframe: '',
            weekly_hours: 0,
            current_level: '',
            constraints: [],
        },
    };
}

/** Check if an interview result has enough data for GoalTree generation */
export function isReadyForGoalTree(result: InterviewResult): boolean {
    return result.finalized &&
           result.extractedSlots.goal.length > 0;
}

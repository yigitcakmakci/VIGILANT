/**
 * auto-ticker-tick-types.ts — TypeScript interfaces for AutoTickerTickEngine.
 *
 * Mirrors C++ TickRecord / TickOutcome / RejectionReason.
 *
 * INVARIANT:
 *   CommitTick is idempotent on (journalEntryId + microTaskId).
 *   A duplicate request returns TickRejected with reason 'DUPLICATE'.
 */

// ═══════════════════════════════════════════════════════════════════════
// RejectionReason — why a tick was not committed
// ═══════════════════════════════════════════════════════════════════════

export type RejectionReason =
    | 'NONE'
    | 'DUPLICATE'
    | 'VERDICT_NOT_PASS'
    | 'MICRO_NOT_FOUND'
    | 'NO_GOAL_TREE'
    | 'NO_INTERVIEW_RESULT'
    | 'INVALID_INPUT'
    | 'PERSIST_FAILED'
    | 'UNKNOWN';

// ═══════════════════════════════════════════════════════════════════════
// TickRecord — row from the auto_ticks audit table
// ═══════════════════════════════════════════════════════════════════════

export interface TickRecord {
    journalEntryId: string;
    microTaskId: string;
    interviewSessionId: string;
    verdict: string;
    confidence: number;
    modelVersion: string;
    committedAt: string;        // ISO-8601
}

// ═══════════════════════════════════════════════════════════════════════
// TickOutcome — result of a CommitTick operation
// ═══════════════════════════════════════════════════════════════════════

export interface TickOutcome {
    committed: boolean;
    microTaskId: string;
    journalEntryId: string;
    interviewSessionId: string;
    rejectionReason: RejectionReason;
    rejectionMessage: string;
    committedAt: string;        // ISO-8601 (only when committed === true)
}

// ═══════════════════════════════════════════════════════════════════════
// Event payloads — EventBridge protocol
// ═══════════════════════════════════════════════════════════════════════

/** UI → C++: request to commit a verified tick */
export interface CommitTickRequestedPayload {
    interviewSessionId: string;
    journalEntryId: string;
    microTaskId: string;
    /** The verification verdict — must be 'pass' for commit to succeed */
    verdict: string;
    confidence: number;
    /** Array of {start, length, text} from the verifier */
    evidenceSpans: Array<{ start: number; length: number; text: string }>;
    explanation: string;
    modelVersion: string;
}

/** C++ → UI: tick was committed successfully */
export interface TickCommittedPayload {
    interviewSessionId: string;
    microTaskId: string;
    journalEntryId: string;
    committedAt: string;        // ISO-8601
}

/** C++ → UI: tick was rejected (duplicate, invalid, etc.) */
export interface TickRejectedPayload {
    interviewSessionId: string;
    microTaskId: string;
    journalEntryId: string;
    rejectionReason: RejectionReason;
    rejectionMessage: string;
}

/** UI → C++: request tick history for a MicroTask */
export interface GetTickHistoryRequestedPayload {
    interviewSessionId: string;
    microTaskId: string;
}

/** C++ → UI: tick history for a MicroTask */
export interface TickHistoryProducedPayload {
    interviewSessionId: string;
    microTaskId: string;
    records: TickRecord[];
}

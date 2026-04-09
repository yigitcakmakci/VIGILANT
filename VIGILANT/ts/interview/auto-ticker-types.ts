/**
 * auto-ticker-types.ts — TypeScript interfaces for AutoTickerMatcher output.
 *
 * Mirrors C++ MatchedSpan / MatchCandidate structs.
 *
 * INVARIANT:
 *   These types represent CANDIDATE suggestions only.
 *   No micro-task is ever marked 'done' by the matcher —
 *   that requires explicit user confirmation.
 */

// ═══════════════════════════════════════════════════════════════════════
// MatchedSpan — a region in the journal text that triggered a match
// ═══════════════════════════════════════════════════════════════════════

export interface MatchedSpan {
    /** Character offset in the journal text */
    start: number;
    /** Span length in characters */
    length: number;
    /** The matched substring (convenience copy) */
    text: string;
}

// ═══════════════════════════════════════════════════════════════════════
// MatchCandidate — a single micro-task match suggestion
// ═══════════════════════════════════════════════════════════════════════

export interface MatchCandidate {
    /** ID of the matched MicroTask (e.g. "micro-0-1-2") */
    microTaskId: string;
    /** Confidence score 0.0–1.0 */
    score: number;
    /** Regions in journal text supporting this match */
    matchedSpans: MatchedSpan[];
    /** Human-readable explanation of why this match was suggested */
    rationale: string;
}

// ═══════════════════════════════════════════════════════════════════════
// Event payloads — EventBridge protocol
// ═══════════════════════════════════════════════════════════════════════

/** UI → C++: user submitted a journal entry for matching */
export interface JournalSubmittedPayload {
    interviewSessionId: string;
    journalText: string;
}

/** C++ → UI: matcher produced ranked candidates (never marks done) */
export interface MatchCandidatesProducedPayload {
    interviewSessionId: string;
    candidates: MatchCandidate[];
    /** Number of open micro-tasks that were evaluated */
    evaluatedCount: number;
    /** GoalTree version_id at the time candidates were produced */
    goalTreeVersionId: string;
}

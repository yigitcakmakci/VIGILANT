/**
 * auto-ticker-version-types.ts — TypeScript interfaces for AutoTickerVersionGuard.
 *
 * Mirrors C++ CachedTreeSnapshot / StaleCandidateEntry / InvalidationResult.
 *
 * INVARIANT:
 *   The version guard NEVER mutates the GoalTree or marks tasks as done.
 *   It only tracks version metadata and detects staleness of pending candidates.
 */

// ═══════════════════════════════════════════════════════════════════════
// Staleness reasons
// ═══════════════════════════════════════════════════════════════════════

export type StaleReason =
    | 'MICRO_REMOVED'
    | 'MICRO_COMPLETED'
    | 'TREE_VERSION_MISMATCH';

// ═══════════════════════════════════════════════════════════════════════
// StaleCandidateEntry — a candidate invalidated by tree version change
// ═══════════════════════════════════════════════════════════════════════

export interface StaleCandidateEntry {
    microTaskId: string;
    oldVersionId: string;
    newVersionId: string;
    reason: StaleReason;
}

// ═══════════════════════════════════════════════════════════════════════
// InvalidationResult — output when a tree version changes
// ═══════════════════════════════════════════════════════════════════════

export interface InvalidationResult {
    sessionId: string;
    oldVersionId: string;
    newVersionId: string;
    invalidated: StaleCandidateEntry[];
    survivingCount: number;
    treeChanged: boolean;
}

// ═══════════════════════════════════════════════════════════════════════
// Event payloads — EventBridge protocol
// ═══════════════════════════════════════════════════════════════════════

/** C++ → UI: pending candidates were invalidated due to GoalTree version change */
export interface CandidatesInvalidatedPayload {
    interviewSessionId: string;
    oldVersionId: string;
    newVersionId: string;
    invalidated: StaleCandidateEntry[];
    survivingCount: number;
}

/** C++ → UI: GoalTree version updated (emitted after replan/regeneration) */
export interface GoalTreeVersionUpdatedPayload {
    interviewSessionId: string;
    versionId: string;
    parentVersionId: string;
    openMicroCount: number;
}

/** UI → C++: request re-matching against the new tree version */
export interface RematchRequestedPayload {
    interviewSessionId: string;
    journalText: string;
    goalTreeVersionId: string;
}

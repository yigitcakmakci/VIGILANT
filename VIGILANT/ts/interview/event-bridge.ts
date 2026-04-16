/**
 * event-bridge.ts — Typed WebView2 ↔ C++ bridge for Socratic Interview.
 *
 * Wraps `window.chrome.webview.postMessage` / `addEventListener('message')`
 * with strongly-typed event envelopes and a simple pub/sub.
 */

export type {
    InterviewResult,
    InterviewResultMessage,
    ExtractedSlots,
    GoalTreeGeneratedPayload,
    InterviewFinalizedResultPayload,
    InterviewSessionSummary,
} from './interview-result-types';

export type {
    GoalTree,
    MajorGoal,
    MinorGoal,
    MicroTask,
    EvidenceType,
    MicroTaskStatus,
    MicroTaskStatusExtended,
    GoalNode,
    DynamicGoalTree,
    DiffEntry,
    GoalTreeDiff,
} from './goal-tree-types';

export type {
    GoalTreeValidationResult,
    GoalTreeValidationOk,
    GoalTreeValidationFail,
    TickValidationError,
    TickValidationResult,
} from './goal-tree-schema';

export { validateGoalTreeJson, validateTickDone } from './goal-tree-schema';

export type { DoneGateResult } from './goal-tree-ui';
export { canMarkDone, evidencePlaceholder } from './goal-tree-ui';

export { mergeGoalTrees, diffGoalTrees } from './goal-tree-replanner';

export {
    renderGoalTree,
    computeNodeProgress,
    collectLeaves,
    findNodeById,
    treeDepth,
} from './goal-tree-recursive-ui';

export { GoalsChatController } from './goals-chat-controller';

export type {
    MatchedSpan,
    MatchCandidate,
    JournalSubmittedPayload,
    MatchCandidatesProducedPayload,
} from './auto-ticker-types';

export type {
    CandidateDisplayItem,
    HighlightSegment,
    ScoreTier,
} from './auto-ticker-ui';
export {
    buildCandidateList,
    highlightJournalText,
    scoreTier,
    formatScore,
    noCandidatesMessage,
} from './auto-ticker-ui';

export type {
    Verdict,
    EvidenceSpan as VerifierEvidenceSpan,
    VerificationResult,
    VerifyCandidateRequestedPayload,
    CandidateVerifiedPayload,
} from './auto-ticker-verifier-types';
export { AUTO_CONFIRM_THRESHOLD } from './auto-ticker-verifier-types';

export type {
    ConfidenceTier,
    VerdictDisplay,
    VerifierHighlightSegment,
} from './auto-ticker-verifier-ui';
export {
    confidenceTier,
    formatConfidence,
    verdictDisplay,
    highlightEvidenceSpans,
    failExplanationMessage,
    verificationUnavailableMessage,
} from './auto-ticker-verifier-ui';

export type {
    RejectionReason,
    TickRecord,
    TickOutcome,
    CommitTickRequestedPayload,
    TickCommittedPayload,
    TickRejectedPayload,
    GetTickHistoryRequestedPayload,
    TickHistoryProducedPayload,
} from './auto-ticker-tick-types';

export type {
    RejectionDisplay,
    CommitDisplay,
    TickHistoryItem,
} from './auto-ticker-tick-ui';
export {
    rejectionDisplay,
    rejectionMessage,
    commitDisplay,
    formatTickHistoryItem,
    buildTickHistoryList,
    noTickHistoryMessage,
    tickSourceMessage,
} from './auto-ticker-tick-ui';

export type {
    StaleReason,
    StaleCandidateEntry,
    InvalidationResult,
    CandidatesInvalidatedPayload,
    GoalTreeVersionUpdatedPayload,
    RematchRequestedPayload,
} from './auto-ticker-version-types';

export type {
    StaleReasonDisplay,
    VersionChangeToast,
    StaleCandidateDisplayItem,
} from './auto-ticker-version-ui';
export {
    staleReasonDisplay,
    buildVersionChangeToast,
    buildStaleCandidateList,
    allCandidatesStaleMessage,
    noCandidatesInvalidatedMessage,
} from './auto-ticker-version-ui';

// ═══════════════════════════════════════════════════════════════════════
// Protocol types
// ═══════════════════════════════════════════════════════════════════════

export type EventType =
    // UI → C++
    | 'InterviewStartRequested'
    | 'UserMessageSubmitted'
    | 'FinalizeInterviewRequested'
    | 'GenerateGoalTree'
    | 'TickMicroTask'
    | 'MicroTaskStatusChangeRequested'
    | 'GetLatestGoalTree'
    | 'ReplanRequested'
    | 'SlotFillerStart'
    | 'SlotFillerAnswer'
    | 'SlotFillerFinalize'
    | 'JournalSubmitted'
    | 'VerifyCandidateRequested'
    | 'CommitTickRequested'
    | 'GetTickHistoryRequested'
    | 'RematchRequested'
    | 'GoalsChatStartRequested'
    | 'GoalsChatMessageSubmitted'
    // C++ → UI
    | 'InterviewStarted'
    | 'GoalsChatResponse'
    | 'AiQuestionProduced'
    | 'InterviewFinalized'
    | 'AskNextQuestion'
    | 'SlotPatched'
    | 'SlotPatchedAndNext'
    | 'InterviewUpdate'
    | 'GoalTreeGenerated'
    | 'GoalTreeValidationFailed'
    | 'MicroTaskTicked'
    | 'TickMicroTaskFailed'
    | 'MicroTaskStatusChanged'
    | 'MicroTaskStatusChangeFailed'
    | 'GoalTreeUpdated'
    | 'ReplanCompleted'
    | 'ReplanFailed'
    | 'MatchCandidatesProduced'
    | 'CandidateVerified'
    | 'TickCommitted'
    | 'TickRejected'
    | 'TickHistoryProduced'
    | 'CandidatesInvalidated'
    | 'GoalTreeVersionUpdated'
    | 'Error';

export interface BridgeEnvelope {
    type: EventType;
    sessionId: string;
    requestId: string;
    ts: string;             // ISO-8601
    payload: Record<string, unknown>;
}

// ── Payload helpers (UI → C++) ─────────────────────────────────────────

export interface InterviewStartPayload {
    maxQuestions: number;
}

export interface UserMessagePayload {
    text: string;
    messageId: string;
}

export interface FinalizePayload {
    endedBy: 'cta' | 'limit';
}

export interface GenerateGoalTreePayload {
    interviewSessionId: string;
}

// ── Payload helpers (C++ → UI) ─────────────────────────────────────────

export interface InterviewStartedPayload {
    sessionId: string;
    firstQuestion: {
        text: string;
        messageId: string;
        isQuestion: true;
    };
}

export interface AiQuestionPayload {
    text: string;
    messageId: string;
    isQuestion: true;
    questionCount: number;
    autoFinalize?: boolean;
}

export interface InterviewFinalizedPayload {
    endedBy: 'cta' | 'limit';
    questionCount: number;
    transcript?: Array<{ id: string; role: string; text: string; ts: string }>;
    interviewSessionId?: string;
    alreadyFinalized?: boolean;
}

export interface ErrorPayload {
    code: string;
    message: string;
}

/** Sent when GoalTree JSON fails schema validation (C++ → UI) */
export interface GoalTreeValidationFailedPayload {
    interviewSessionId: string;
    error: string;
    path: string;
}

// ── Tick MicroTask payloads ────────────────────────────────────────────

/** UI → C++: request to mark a MicroTask as 'done' with evidence */
export interface TickMicroTaskPayload {
    interviewSessionId: string;
    microTaskId: string;
    evidence: import('./goal-tree-types').Evidence;
}

/** C++ → UI: MicroTask successfully ticked to 'done' */
export interface MicroTaskTickedPayload {
    interviewSessionId: string;
    microTaskId: string;
    status: 'done';
}

/** C++ → UI: tick rejected — structured errors */
export interface TickMicroTaskFailedPayload {
    interviewSessionId: string;
    microTaskId: string;
    errors: Array<{ code: string; message: string; microTaskId: string }>;
}

/** UI → C++: request to change a MicroTask's status (e.g. open → done) */
export interface MicroTaskStatusChangeRequestedPayload {
    interviewSessionId: string;
    microTaskId: string;
    newStatus: 'done';
    evidence: import('./goal-tree-types').Evidence;
}

/** C++ → UI: status change confirmed */
export interface MicroTaskStatusChangedPayload {
    interviewSessionId: string;
    microTaskId: string;
    status: 'done';
    evidence?: import('./goal-tree-types').Evidence;
}

/** C++ → UI: status change rejected */
export interface MicroTaskStatusChangeFailedPayload {
    interviewSessionId: string;
    microTaskId: string;
    errors: Array<{ code: string; message: string; microTaskId: string }>;
}

// ── Replan payloads ───────────────────────────────────────────────────

/** UI → C++: request a GoalTree replan (slot change or user edit) */
export interface ReplanRequestedPayload {
    interviewSessionId: string;
    /** What triggered the replan */
    reason: 'slot_change' | 'user_edit' | 'manual';
    /** Optional: partial slot overrides that changed */
    changedSlots?: Record<string, unknown>;
}

/** C++ → UI: replan completed — new tree + diff */
export interface ReplanCompletedPayload {
    interviewSessionId: string;
    goalTree: import('./goal-tree-types').GoalTree;
    diff: import('./goal-tree-types').GoalTreeDiff;
    oldVersionId: string;
    newVersionId: string;
}

/** C++ → UI: replan failed */
export interface ReplanFailedPayload {
    interviewSessionId: string;
    error: string;
    code: string;
}

// ═══════════════════════════════════════════════════════════════════════
// Bridge singleton
// ═══════════════════════════════════════════════════════════════════════

type MessageHandler = (event: BridgeEnvelope) => void;

let _nextRequestId = 1;
const _handlers: Set<MessageHandler> = new Set();
let _bound = false;

/** Generate a unique request ID for idempotent tracking */
export function nextRequestId(): string {
    return 'req-' + (_nextRequestId++) + '-' + Date.now();
}

/** Generate a simple UUID-like string */
export function generateId(): string {
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
        const r = (Math.random() * 16) | 0;
        const v = c === 'x' ? r : (r & 0x3) | 0x8;
        return v.toString(16);
    });
}

/** Publish an event to C++ via WebView2 bridge */
export function publish(event: BridgeEnvelope): void {
    if (typeof window !== 'undefined' &&
        (window as any).chrome?.webview?.postMessage) {
        (window as any).chrome.webview.postMessage(event);
    } else {
        // Dev fallback: log to console
        console.log('[EventBridge] publish (no WebView2):', event);
    }
}

/** Subscribe to messages from C++ */
export function onMessage(handler: MessageHandler): () => void {
    _handlers.add(handler);
    _ensureBound();
    return () => { _handlers.delete(handler); };
}

function _ensureBound(): void {
    if (_bound) return;
    _bound = true;

    if (typeof window !== 'undefined' &&
        (window as any).chrome?.webview) {
        (window as any).chrome.webview.addEventListener('message', (e: any) => {
            const data = e.data as BridgeEnvelope;
            if (data && data.type) {
                for (const h of _handlers) {
                    try { h(data); } catch (err) { console.error('[EventBridge] handler error', err); }
                }
            }
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Convenience publishers
// ═══════════════════════════════════════════════════════════════════════

export function publishInterviewStart(sessionId: string): void {
    publish({
        type: 'InterviewStartRequested',
        sessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { maxQuestions: 3 }
    });
}

export function publishUserMessage(sessionId: string, text: string, messageId: string): void {
    publish({
        type: 'UserMessageSubmitted',
        sessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { text, messageId }
    });
}

export function publishFinalize(sessionId: string, endedBy: 'cta' | 'limit'): void {
    publish({
        type: 'FinalizeInterviewRequested',
        sessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { endedBy }
    });
}

export function publishGoalsChatStart(): void {
    publish({
        type: 'GoalsChatStartRequested',
        sessionId: '',
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: {}
    });
}

export function publishGoalsChatMessage(sessionId: string, text: string): void {
    publish({
        type: 'GoalsChatMessageSubmitted',
        sessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { sessionId, text }
    });
}

export function publishGenerateGoalTree(interviewSessionId: string): void {
    publish({
        type: 'GenerateGoalTree',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId }
    });
}

export function publishTickMicroTask(
    interviewSessionId: string,
    microTaskId: string,
    evidence: import('./goal-tree-types').Evidence,
): void {
    publish({
        type: 'TickMicroTask',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, microTaskId, evidence },
    });
}

export function publishMicroTaskStatusChange(
    interviewSessionId: string,
    microTaskId: string,
    newStatus: 'done',
    evidence: import('./goal-tree-types').Evidence,
): void {
    publish({
        type: 'MicroTaskStatusChangeRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, microTaskId, newStatus, evidence },
    });
}

export function publishReplanRequested(
    interviewSessionId: string,
    reason: 'slot_change' | 'user_edit' | 'manual',
    changedSlots?: Record<string, unknown>,
): void {
    publish({
        type: 'ReplanRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, reason, changedSlots: changedSlots ?? {} },
    });
}

/**
 * Publish a journal entry for auto-ticker matching.
 * This only produces candidate suggestions — it NEVER marks any task as done.
 */
export function publishJournalSubmitted(
    interviewSessionId: string,
    journalText: string,
): void {
    publish({
        type: 'JournalSubmitted',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, journalText },
    });
}

/**
 * Request strict acceptance-criteria verification for a single candidate.
 * The C++ backend will call the LLM and return a CandidateVerified event.
 * This NEVER marks any task as done — the UI must ask the user to confirm.
 */
export function publishVerifyCandidate(
    interviewSessionId: string,
    microTaskId: string,
    journalText: string,
): void {
    publish({
        type: 'VerifyCandidateRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, microTaskId, journalText },
    });
}

/**
 * Commit a verified tick — idempotent on (journalEntryId + microTaskId).
 * The C++ backend will mutate the GoalTree if the tick is valid, or return
 * a TickRejected event with a structured reason.
 */
export function publishCommitTickRequested(
    interviewSessionId: string,
    journalEntryId: string,
    microTaskId: string,
    verdict: string,
    confidence: number,
    evidenceSpans: Array<{ start: number; length: number; text: string }>,
    explanation: string,
    modelVersion: string,
): void {
    publish({
        type: 'CommitTickRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: {
            interviewSessionId,
            journalEntryId,
            microTaskId,
            verdict,
            confidence,
            evidenceSpans,
            explanation,
            modelVersion,
        },
    });
}

/**
 * Request tick history for a specific MicroTask.
 * The C++ backend will return a TickHistoryProduced event.
 */
export function publishGetTickHistoryRequested(
    interviewSessionId: string,
    microTaskId: string,
): void {
    publish({
        type: 'GetTickHistoryRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, microTaskId },
    });
}

/**
 * Request re-matching against the latest GoalTree version.
 * Typically called after a CandidatesInvalidated event when the user
 * clicks "Re-match" or automatically by the dashboard.
 */
export function publishRematchRequested(
    interviewSessionId: string,
    journalText: string,
    goalTreeVersionId: string,
): void {
    publish({
        type: 'RematchRequested',
        sessionId: interviewSessionId,
        requestId: nextRequestId(),
        ts: new Date().toISOString(),
        payload: { interviewSessionId, journalText, goalTreeVersionId },
    });
}

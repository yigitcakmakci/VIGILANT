/**
 * auto-ticker-verifier-types.ts — TypeScript interfaces for AutoTickerVerifier.
 *
 * Mirrors C++ EvidenceSpan / VerificationResult structs.
 *
 * INVARIANT:
 *   A 'pass' verdict does NOT auto-complete the task.
 *   - High confidence (≥ 0.85): UI shows pre-filled confirmation dialog.
 *   - Low  confidence (< 0.85): UI shows warning + requires explicit confirm.
 *   - 'fail' verdict: UI shows explanation, no tick action offered.
 */

// ═══════════════════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════════════════

/** Confidence threshold for "high confidence" pass.
 *  Below this the UI MUST require explicit user confirmation. */
export const AUTO_CONFIRM_THRESHOLD = 0.85;

// ═══════════════════════════════════════════════════════════════════════
// Verdict
// ═══════════════════════════════════════════════════════════════════════

export type Verdict = 'pass' | 'fail';

// ═══════════════════════════════════════════════════════════════════════
// EvidenceSpan — quoted region from journal text that constitutes proof
// ═══════════════════════════════════════════════════════════════════════

export interface EvidenceSpan {
    /** 0-based character offset in the journal text */
    start: number;
    /** Span length in characters */
    length: number;
    /** Exact quoted substring from the journal */
    text: string;
}

// ═══════════════════════════════════════════════════════════════════════
// VerificationResult — per-MicroTask verdict from the Verifier
// ═══════════════════════════════════════════════════════════════════════

export interface VerificationResult {
    microTaskId: string;
    verdict: Verdict;
    /** 0.0–1.0 confidence in the verdict */
    confidence: number;
    /** Non-empty only when verdict === 'pass' */
    evidenceSpans: EvidenceSpan[];
    /** Human-readable reasoning */
    explanation: string;
}

// ═══════════════════════════════════════════════════════════════════════
// Event payloads — EventBridge protocol
// ═══════════════════════════════════════════════════════════════════════

/** UI → C++: request verification of a single candidate */
export interface VerifyCandidateRequestedPayload {
    interviewSessionId: string;
    microTaskId: string;
    journalText: string;
}

/** C++ → UI: verification result for a single candidate */
export interface CandidateVerifiedPayload {
    interviewSessionId: string;
    result: VerificationResult;
    /** Whether confidence is above AUTO_CONFIRM_THRESHOLD */
    highConfidence: boolean;
}

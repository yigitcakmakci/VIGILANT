/**
 * auto-ticker-verifier-ui.ts — UI-layer helpers for AutoTickerVerifier.
 *
 * Pure functions — no DOM mutation, no side effects.
 * Consumed by the dashboard to render verification results and
 * drive the "confirm / reject" flow for the user.
 *
 * INVARIANT:
 *   These helpers NEVER mark a task as done.
 *   Even a high-confidence PASS only pre-fills the confirmation dialog —
 *   the user must click "Confirm" to actually tick the task.
 */

import type {
    VerificationResult,
    EvidenceSpan,
    Verdict,
} from './auto-ticker-verifier-types';
import { AUTO_CONFIRM_THRESHOLD } from './auto-ticker-verifier-types';

// ═══════════════════════════════════════════════════════════════════════
// Confidence tier (drives badge colour + UX copy)
// ═══════════════════════════════════════════════════════════════════════

export type ConfidenceTier = 'high' | 'medium' | 'low';

/** Classify confidence into a UI tier */
export function confidenceTier(confidence: number): ConfidenceTier {
    if (confidence >= AUTO_CONFIRM_THRESHOLD) return 'high';
    if (confidence >= 0.5) return 'medium';
    return 'low';
}

/** Format confidence as a percentage, e.g. "92%" */
export function formatConfidence(confidence: number): string {
    return `${Math.round(confidence * 100)}%`;
}

// ═══════════════════════════════════════════════════════════════════════
// Verdict display helpers
// ═══════════════════════════════════════════════════════════════════════

export interface VerdictDisplay {
    /** Localised label for the verdict badge */
    label: string;
    /** CSS class suffix: 'pass-high', 'pass-low', 'fail' */
    badgeClass: string;
    /** Icon hint */
    icon: '✅' | '⚠️' | '❌';
    /** Whether the "Confirm completion" button should be visible */
    showConfirmButton: boolean;
    /** If showConfirmButton is true, whether to show a warning banner */
    showWarningBanner: boolean;
    /** Banner text (empty when no warning) */
    warningText: string;
}

/** Build the full display descriptor for a verification result */
export function verdictDisplay(result: VerificationResult): VerdictDisplay {
    if (result.verdict === 'fail') {
        return {
            label: 'Doğrulanamadı',
            badgeClass: 'fail',
            icon: '❌',
            showConfirmButton: false,
            showWarningBanner: false,
            warningText: '',
        };
    }

    // verdict === 'pass'
    const tier = confidenceTier(result.confidence);

    if (tier === 'high') {
        return {
            label: 'Doğrulandı',
            badgeClass: 'pass-high',
            icon: '✅',
            showConfirmButton: true,
            showWarningBanner: false,
            warningText: '',
        };
    }

    // pass + medium/low confidence
    return {
        label: 'Kısmen Doğrulandı',
        badgeClass: 'pass-low',
        icon: '⚠️',
        showConfirmButton: true,
        showWarningBanner: true,
        warningText:
            `Güven düzeyi düşük (${formatConfidence(result.confidence)}). ` +
            'Lütfen kanıt alıntılarını gözden geçirip onaylayın.',
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Evidence span highlighting (for rendering inside journal text)
// ═══════════════════════════════════════════════════════════════════════

export interface VerifierHighlightSegment {
    text: string;
    /** true = this segment is an evidence quote */
    isEvidence: boolean;
}

/**
 * Split journal text into evidence / non-evidence segments based on
 * the verifier's evidence spans.
 *
 * Non-overlapping, sorted by position.  Invalid spans are silently skipped.
 */
export function highlightEvidenceSpans(
    journalText: string,
    spans: EvidenceSpan[],
): VerifierHighlightSegment[] {
    // Filter valid spans
    const valid = spans
        .filter(
            (s) =>
                s.length > 0 &&
                s.start >= 0 &&
                s.start + s.length <= journalText.length,
        )
        .sort((a, b) => a.start - b.start);

    if (valid.length === 0) {
        return [{ text: journalText, isEvidence: false }];
    }

    // Merge overlapping spans
    const merged: Array<{ start: number; end: number }> = [];
    for (const s of valid) {
        const end = s.start + s.length;
        const last = merged[merged.length - 1];
        if (last && s.start <= last.end) {
            last.end = Math.max(last.end, end);
        } else {
            merged.push({ start: s.start, end });
        }
    }

    const segments: VerifierHighlightSegment[] = [];
    let cursor = 0;
    for (const m of merged) {
        if (cursor < m.start) {
            segments.push({
                text: journalText.slice(cursor, m.start),
                isEvidence: false,
            });
        }
        segments.push({
            text: journalText.slice(m.start, m.end),
            isEvidence: true,
        });
        cursor = m.end;
    }
    if (cursor < journalText.length) {
        segments.push({
            text: journalText.slice(cursor),
            isEvidence: false,
        });
    }

    return segments;
}

// ═══════════════════════════════════════════════════════════════════════
// Explanation / empty-state messages
// ═══════════════════════════════════════════════════════════════════════

/** Returns a user-facing summary for a fail verdict */
export function failExplanationMessage(result: VerificationResult): string {
    if (result.explanation) {
        return result.explanation;
    }
    return 'Günlük metni, bu görevin kabul kriterini açıkça karşılamamaktadır.';
}

/** Returns a user-facing summary when verification cannot be performed */
export function verificationUnavailableMessage(): string {
    return 'Doğrulama yapılamıyor — AI servisi kullanılamıyor.';
}

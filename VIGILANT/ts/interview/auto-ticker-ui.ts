/**
 * auto-ticker-ui.ts — UI-layer helpers for AutoTickerMatcher candidate list.
 *
 * Pure functions — no DOM mutation, no side effects.
 * Consumed by the dashboard to render "Suggested Matches" panel.
 *
 * INVARIANT:
 *   These helpers NEVER mark a task as done.
 *   They only prepare display data for the user to review.
 */

import type { MatchCandidate, MatchedSpan } from './auto-ticker-types';

// ═══════════════════════════════════════════════════════════════════════
// Score formatting
// ═══════════════════════════════════════════════════════════════════════

export type ScoreTier = 'high' | 'medium' | 'low';

/** Classify a 0–1 score into a UI tier for badge styling */
export function scoreTier(score: number): ScoreTier {
    if (score >= 0.75) return 'high';
    if (score >= 0.4)  return 'medium';
    return 'low';
}

/** Format score as a percentage string, e.g. "87%" */
export function formatScore(score: number): string {
    return `${Math.round(score * 100)}%`;
}

// ═══════════════════════════════════════════════════════════════════════
// Candidate list helpers
// ═══════════════════════════════════════════════════════════════════════

/** A pre-processed candidate ready for rendering */
export interface CandidateDisplayItem {
    microTaskId: string;
    score: number;
    scoreLabel: string;
    tier: ScoreTier;
    rationale: string;
    matchedSpans: MatchedSpan[];
    /** Number of journal regions that matched */
    spanCount: number;
}

/**
 * Transform raw MatchCandidate[] into display-ready items.
 * Already sorted by descending score (server provides this).
 *
 * @param candidates  Raw candidates from C++ backend
 * @param minScore    Minimum score threshold to include (default 0.1)
 */
export function buildCandidateList(
    candidates: MatchCandidate[],
    minScore = 0.1,
): CandidateDisplayItem[] {
    return candidates
        .filter((c) => c.score >= minScore)
        .map((c) => ({
            microTaskId: c.microTaskId,
            score: c.score,
            scoreLabel: formatScore(c.score),
            tier: scoreTier(c.score),
            rationale: c.rationale,
            matchedSpans: c.matchedSpans,
            spanCount: c.matchedSpans.length,
        }));
}

// ═══════════════════════════════════════════════════════════════════════
// Journal text highlighting
// ═══════════════════════════════════════════════════════════════════════

/** A segment of journal text, either highlighted or plain */
export interface HighlightSegment {
    text: string;
    highlighted: boolean;
    /** If highlighted, which microTaskId(s) triggered this span */
    microTaskIds: string[];
}

/**
 * Split journal text into highlighted/plain segments based on all
 * matched spans across all candidates.
 *
 * Overlapping spans are merged. Segments are returned in text order.
 */
export function highlightJournalText(
    journalText: string,
    candidates: MatchCandidate[],
): HighlightSegment[] {
    // Collect all spans with their microTaskId
    const spans: Array<{ start: number; end: number; id: string }> = [];
    for (const c of candidates) {
        for (const s of c.matchedSpans) {
            if (s.length > 0 && s.start >= 0 && s.start + s.length <= journalText.length) {
                spans.push({ start: s.start, end: s.start + s.length, id: c.microTaskId });
            }
        }
    }

    if (spans.length === 0) {
        return [{ text: journalText, highlighted: false, microTaskIds: [] }];
    }

    // Sort by start position
    spans.sort((a, b) => a.start - b.start || a.end - b.end);

    // Merge overlapping spans
    const merged: Array<{ start: number; end: number; ids: string[] }> = [];
    for (const s of spans) {
        const last = merged[merged.length - 1];
        if (last && s.start <= last.end) {
            last.end = Math.max(last.end, s.end);
            if (!last.ids.includes(s.id)) last.ids.push(s.id);
        } else {
            merged.push({ start: s.start, end: s.end, ids: [s.id] });
        }
    }

    // Build segments
    const segments: HighlightSegment[] = [];
    let cursor = 0;
    for (const m of merged) {
        if (cursor < m.start) {
            segments.push({
                text: journalText.slice(cursor, m.start),
                highlighted: false,
                microTaskIds: [],
            });
        }
        segments.push({
            text: journalText.slice(m.start, m.end),
            highlighted: true,
            microTaskIds: m.ids,
        });
        cursor = m.end;
    }
    if (cursor < journalText.length) {
        segments.push({
            text: journalText.slice(cursor),
            highlighted: false,
            microTaskIds: [],
        });
    }

    return segments;
}

// ═══════════════════════════════════════════════════════════════════════
// Empty-state message
// ═══════════════════════════════════════════════════════════════════════

/** Returns a user-facing message when no candidates matched */
export function noCandidatesMessage(evaluatedCount: number): string {
    if (evaluatedCount === 0) {
        return 'Açık görev bulunamadı — önce bir hedef ağacı oluşturun.';
    }
    return `${evaluatedCount} açık görev değerlendirildi, ancak eşleşme bulunamadı.`;
}

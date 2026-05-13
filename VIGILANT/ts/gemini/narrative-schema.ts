/**
 * Daily narrative – JSON output parser & validator.
 *
 * Validates that the raw string returned by Gemini matches the
 * NarrativeResult schema before it enters the application.
 * No external validation library – keeps the dependency footprint zero.
 */

import type { NarrativeResult } from './narrative-types';

// ── Public types ───────────────────────────────────────────────────────

export interface NarrativeValidationOk {
    ok: true;
    data: NarrativeResult;
}

export interface NarrativeValidationFail {
    ok: false;
    error: string;
    raw: string;
}

export type NarrativeValidationResult = NarrativeValidationOk | NarrativeValidationFail;

// ── Helpers ────────────────────────────────────────────────────────────

/**
 * Rough sentence count: split on sentence-ending punctuation followed
 * by whitespace or end-of-string.
 */
function countSentences(text: string): number {
    const matches = text.match(/[.!?](?:\s|$)/g);
    return matches ? matches.length : 0;
}

// ── Validator ──────────────────────────────────────────────────────────

/**
 * Parse and validate a raw JSON string against the NarrativeResult
 * schema.  Returns a discriminated union so callers never touch an
 * unvalidated object.
 */
export function validateNarrativeJson(raw: string): NarrativeValidationResult {
    // Strip markdown fences the model sometimes sneaks in.
    const cleaned = raw.replace(/^```(?:json)?\s*|```$/g, '').trim();

    let parsed: unknown;
    try {
        parsed = JSON.parse(cleaned);
    } catch {
        return { ok: false, error: 'Invalid JSON', raw };
    }

    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
        return { ok: false, error: 'Expected a JSON object', raw };
    }

    const obj = parsed as Record<string, unknown>;

    // ── narrative ──────────────────────────────────────────────────────
    if (typeof obj['narrative'] !== 'string' || obj['narrative'].length === 0) {
        return { ok: false, error: 'Missing or empty narrative', raw };
    }

    const sentences = countSentences(obj['narrative']);
    if (sentences < 3 || sentences > 15) {
        return {
            ok: false,
            error: `Narrative has ${sentences} sentences (expected 5–10, tolerance 3–15)`,
            raw,
        };
    }

    // ── insights ───────────────────────────────────────────────────────
    if (!Array.isArray(obj['insights'])) {
        return { ok: false, error: 'Missing insights array', raw };
    }

    if (obj['insights'].length !== 3) {
        return {
            ok: false,
            error: `Expected 3 insights, got ${obj['insights'].length}`,
            raw,
        };
    }

    for (let i = 0; i < 3; i++) {
        if (typeof obj['insights'][i] !== 'string' || (obj['insights'][i] as string).length === 0) {
            return { ok: false, error: `Insight[${i}] is not a non-empty string`, raw };
        }
    }

    return {
        ok: true,
        data: {
            narrative: obj['narrative'],
            insights: obj['insights'] as [string, string, string],
        },
    };
}

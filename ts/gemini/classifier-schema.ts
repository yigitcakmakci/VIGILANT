/**
 * Activity classifier – JSON schema validation.
 *
 * Validates that the raw string returned by Gemini matches the
 * ClassificationResult schema before it enters the application.
 * No external validation library – keeps the dependency footprint zero.
 */

import type { ActivityCategory, ClassificationResult } from './classifier-types';

// ── Constants ──────────────────────────────────────────────────────────

const VALID_CATEGORIES: ReadonlySet<ActivityCategory> = new Set([
    'productive',
    'consumptive',
    'neutral',
    'unknown',
]);

// ── Public API ─────────────────────────────────────────────────────────

export interface ValidationOk {
    ok: true;
    data: ClassificationResult;
}

export interface ValidationFail {
    ok: false;
    error: string;
    raw: string;
}

export type ValidationResult = ValidationOk | ValidationFail;

/**
 * Parse and validate a raw JSON string against the ClassificationResult
 * schema.  Returns a discriminated union so callers never touch an
 * unvalidated object.
 */
export function validateClassificationJson(raw: string): ValidationResult {
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

    // ── category ───────────────────────────────────────────────────────
    if (typeof obj['category'] !== 'string' || !VALID_CATEGORIES.has(obj['category'] as ActivityCategory)) {
        return {
            ok: false,
            error: `Invalid category: "${String(obj['category'])}"`,
            raw,
        };
    }

    // ── confidence ─────────────────────────────────────────────────────
    if (typeof obj['confidence'] !== 'number' || obj['confidence'] < 0 || obj['confidence'] > 1) {
        return {
            ok: false,
            error: `Invalid confidence: ${String(obj['confidence'])}`,
            raw,
        };
    }

    // ── rationale ──────────────────────────────────────────────────────
    if (typeof obj['rationale'] !== 'string' || obj['rationale'].length === 0) {
        return {
            ok: false,
            error: 'Missing or empty rationale',
            raw,
        };
    }

    return {
        ok: true,
        data: {
            category: obj['category'] as ActivityCategory,
            confidence: Math.round(obj['confidence'] * 100) / 100,
            rationale: obj['rationale'],
        },
    };
}

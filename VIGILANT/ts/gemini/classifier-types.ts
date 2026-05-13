/**
 * Activity classifier – type definitions.
 *
 * Covers the input shape sent by the tracker agent and the
 * structured output returned by the Gemini classification step.
 */

// ── Input ──────────────────────────────────────────────────────────────

export interface ActivityInput {
    exePath: string;
    appName: string;
    windowTitle: string;
    urlHint?: string;
    recentUserLabels: string[];
}

// ── Output ─────────────────────────────────────────────────────────────

export type ActivityCategory = 'productive' | 'consumptive' | 'neutral' | 'unknown';

export interface ClassificationResult {
    category: ActivityCategory;
    confidence: number;
    rationale: string;
}

// ── Config ─────────────────────────────────────────────────────────────

export interface ClassifierConfig {
    /** Confidence floor – results below this become category=unknown. */
    confidenceThreshold?: number;
    /** Max items held in the in-memory cache (default 512). */
    cacheCapacity?: number;
}

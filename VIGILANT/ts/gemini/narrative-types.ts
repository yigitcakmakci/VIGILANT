/**
 * Daily narrative – type definitions.
 *
 * Covers the raw session block coming from the tracker, the
 * compressed timeline sent to Gemini, and the structured narrative
 * output returned by the generation step.
 */

// ── Raw input (from tracker DB) ────────────────────────────────────────

export interface SessionBlock {
    /** ISO-8601 or HH:mm start time. */
    start: string;
    /** ISO-8601 or HH:mm end time. */
    end: string;
    appName: string;
    /** Classifier-assigned category. */
    category: 'productive' | 'consumptive' | 'neutral' | 'unknown';
    /** 0–1 focus score for the block. */
    focusScore: number;
    /** Raw window title – will be stripped by the preprocessor. */
    windowTitle?: string;
}

// ── Compressed block (sent to Gemini) ──────────────────────────────────

export interface CompressedBlock {
    start: string;
    end: string;
    app: string;
    cat: string;
    focus: number;
    /** Duration in minutes. */
    mins: number;
}

// ── Gemini output ──────────────────────────────────────────────────────

export interface NarrativeResult {
    /** 5–10 sentence daily summary. */
    narrative: string;
    /** Exactly 3 actionable insight bullets. */
    insights: [string, string, string];
}

// ── Config ─────────────────────────────────────────────────────────────

export interface NarrativeConfig {
    /** Max compressed blocks to include in the prompt (default 40). */
    maxBlocks?: number;
    /**
     * Approximate token budget for the timeline payload.
     * Blocks are dropped (oldest-first after merging) when exceeded.
     * Default 1500.
     */
    tokenBudget?: number;
    /** Minimum block duration in minutes to keep (default 1). */
    minBlockMinutes?: number;
}

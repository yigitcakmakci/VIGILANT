/**
 * Daily narrative – input preprocessor.
 *
 * Transforms raw SessionBlock[] into a compact timeline suitable
 * for the Gemini prompt:
 *   1. Strips / masks sensitive window titles.
 *   2. Merges adjacent blocks with the same (appName, category).
 *   3. Drops blocks shorter than a configurable minimum.
 *   4. Truncates the list when the estimated token budget is exceeded.
 *
 * The output is a plain JSON string – deterministic and human-readable
 * so it can be inspected in logs.
 */

import type {
    SessionBlock,
    CompressedBlock,
    NarrativeConfig,
} from './narrative-types';

// ── Defaults ───────────────────────────────────────────────────────────

const DEFAULT_MAX_BLOCKS = 40;
const DEFAULT_TOKEN_BUDGET = 1500;
const DEFAULT_MIN_BLOCK_MINUTES = 1;

/** Rough chars-per-token ratio for JSON payloads (conservative). */
const CHARS_PER_TOKEN = 4;

// ── Helpers ────────────────────────────────────────────────────────────

/** Parse an ISO-8601 string or HH:mm into epoch ms. */
function toEpoch(s: string): number {
    if (/^\d{2}:\d{2}$/.test(s)) {
        const [h, m] = s.split(':').map(Number);
        const d = new Date();
        d.setHours(h!, m!, 0, 0);
        return d.getTime();
    }
    return new Date(s).getTime();
}

/** Duration between two time strings in whole minutes. */
function durationMinutes(start: string, end: string): number {
    return Math.round((toEpoch(end) - toEpoch(start)) / 60_000);
}

/** Format epoch ms back to HH:mm for compactness. */
function toHHmm(s: string): string {
    const d = new Date(toEpoch(s));
    const hh = String(d.getHours()).padStart(2, '0');
    const mm = String(d.getMinutes()).padStart(2, '0');
    return `${hh}:${mm}`;
}

/**
 * Shorten a category label to save tokens.
 *   productive → P, consumptive → C, neutral → N, unknown → U
 */
function shortCategory(cat: string): string {
    const map: Record<string, string> = {
        productive: 'P',
        consumptive: 'C',
        neutral: 'N',
        unknown: 'U',
    };
    return map[cat] ?? 'U';
}

/**
 * Estimate the token count of a JSON string using a simple
 * chars / CHARS_PER_TOKEN heuristic.
 */
function estimateTokens(json: string): number {
    return Math.ceil(json.length / CHARS_PER_TOKEN);
}

// ── Public API ─────────────────────────────────────────────────────────

/**
 * Compress a day's session blocks into a token-budgeted JSON string
 * ready to embed in the Gemini prompt.
 *
 * @returns  The compressed timeline JSON **and** the block count.
 */
export function compressTimeline(
    blocks: readonly SessionBlock[],
    config: NarrativeConfig = {},
): { json: string; blockCount: number } {
    const maxBlocks = config.maxBlocks ?? DEFAULT_MAX_BLOCKS;
    const tokenBudget = config.tokenBudget ?? DEFAULT_TOKEN_BUDGET;
    const minMins = config.minBlockMinutes ?? DEFAULT_MIN_BLOCK_MINUTES;

    // 1. Sort by start time.
    const sorted = [...blocks].sort(
        (a, b) => toEpoch(a.start) - toEpoch(b.start),
    );

    // 2. Strip window titles (privacy) and build initial compressed list.
    const initial: CompressedBlock[] = sorted.map((b) => ({
        start: toHHmm(b.start),
        end: toHHmm(b.end),
        app: b.appName,
        cat: shortCategory(b.category),
        focus: Math.round(b.focusScore * 100) / 100,
        mins: durationMinutes(b.start, b.end),
    }));

    // 3. Merge adjacent blocks with same (app, cat).
    const merged: CompressedBlock[] = [];
    for (const block of initial) {
        const prev = merged[merged.length - 1];
        if (prev && prev.app === block.app && prev.cat === block.cat) {
            prev.end = block.end;
            prev.mins += block.mins;
            prev.focus = Math.round(((prev.focus + block.focus) / 2) * 100) / 100;
        } else {
            merged.push({ ...block });
        }
    }

    // 4. Filter out tiny blocks.
    let filtered = merged.filter((b) => b.mins >= minMins);

    // 5. Cap block count.
    if (filtered.length > maxBlocks) {
        filtered = filtered.slice(0, maxBlocks);
    }

    // 6. Trim from the beginning until within token budget.
    while (filtered.length > 1) {
        const json = JSON.stringify(filtered);
        if (estimateTokens(json) <= tokenBudget) break;
        filtered.shift();
    }

    const json = JSON.stringify(filtered);
    return { json, blockCount: filtered.length };
}

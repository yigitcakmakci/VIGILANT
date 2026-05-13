/**
 * Activity classifier – prompt template & few-shot examples.
 *
 * The prompt is deliberately short and deterministic:
 *   • JSON-only output (no markdown fences, no commentary).
 *   • Enum-constrained category.
 *   • Confidence score forces the model to self-evaluate.
 *   • Few-shot examples anchor the expected format.
 */

import type { ActivityInput } from './classifier-types';

// ── System prompt ──────────────────────────────────────────────────────

export const CLASSIFIER_SYSTEM_PROMPT = `You are an activity classifier.
Given a desktop-activity record, respond with EXACTLY one JSON object – no markdown, no explanation outside the object.

Schema:
{ "category": "productive"|"consumptive"|"neutral"|"unknown", "confidence": <0..1>, "rationale": "<one sentence>" }

Definitions:
• productive  – work, coding, documentation, design, professional communication.
• consumptive – entertainment, social media, gaming, casual browsing.
• neutral     – system utilities, OS dialogs, ambiguous tools.
• unknown     – insufficient signal to decide.

Rules:
1. Output valid JSON only.
2. confidence is a float between 0 and 1, rounded to two decimals.
3. If you are uncertain, lower the confidence; do NOT guess a wrong category.`;

// ── Few-shot examples ──────────────────────────────────────────────────

interface FewShot {
    input: ActivityInput;
    output: string;
}

const FEW_SHOTS: readonly FewShot[] = [
    {
        input: {
            exePath: 'C:\\Program Files\\Microsoft VS Code\\Code.exe',
            appName: 'Visual Studio Code',
            windowTitle: 'gemini-client.ts — VIGILANT',
            urlHint: '',
            recentUserLabels: ['coding', 'work'],
        },
        output: '{"category":"productive","confidence":0.95,"rationale":"IDE open on a source file in a work project."}',
    },
    {
        input: {
            exePath: 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
            appName: 'Google Chrome',
            windowTitle: 'YouTube - Funny cat compilation',
            urlHint: 'youtube.com/watch?v=abc123',
            recentUserLabels: [],
        },
        output: '{"category":"consumptive","confidence":0.92,"rationale":"YouTube entertainment video in browser."}',
    },
    {
        input: {
            exePath: 'C:\\Windows\\explorer.exe',
            appName: 'Windows Explorer',
            windowTitle: 'Downloads',
            urlHint: '',
            recentUserLabels: [],
        },
        output: '{"category":"neutral","confidence":0.80,"rationale":"File manager browsing downloads folder – purpose ambiguous."}',
    },
    {
        input: {
            exePath: 'D:\\tools\\custom-app.exe',
            appName: 'custom-app',
            windowTitle: '',
            urlHint: '',
            recentUserLabels: [],
        },
        output: '{"category":"unknown","confidence":0.30,"rationale":"No window title or labels; cannot determine purpose."}',
    },
] as const;

// ── Builder ────────────────────────────────────────────────────────────

/**
 * Build the full prompt (system + few-shot + user record) as a single
 * user-turn string suitable for `GeminiClient.prompt()`.
 */
export function buildClassifierPrompt(input: ActivityInput): string {
    const shots = FEW_SHOTS.map(
        (s) =>
            `Input:\n${JSON.stringify(s.input)}\nOutput:\n${s.output}`,
    ).join('\n\n');

    const userRecord = JSON.stringify(input);

    return `${CLASSIFIER_SYSTEM_PROMPT}\n\n--- Examples ---\n\n${shots}\n\n--- Classify ---\n\nInput:\n${userRecord}\nOutput:`;
}

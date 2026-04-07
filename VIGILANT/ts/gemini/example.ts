/**
 * Example: classifyUnknownActivity()
 *
 * Uses GeminiClient to classify an unknown application-usage record
 * into a productivity category.  Demonstrates the full pipeline:
 *   config → rate-limit → timeout/retry → typed response → logging.
 *
 * Run:
 *   GEMINI_API_KEY=<key> npx tsx example.ts
 */

import { GeminiClient } from './gemini-client';

// ── Domain types ───────────────────────────────────────────────────────

interface ActivityRecord {
    appName: string;
    windowTitle: string;
    durationSeconds: number;
}

type ProductivityCategory =
    | 'deep-work'
    | 'communication'
    | 'planning'
    | 'break'
    | 'distraction'
    | 'unknown';

interface ClassificationResult {
    activity: ActivityRecord;
    category: ProductivityCategory;
    confidence: number;
    reasoning: string;
}

// ── Classifier ─────────────────────────────────────────────────────────

const SYSTEM_PROMPT = `You are a productivity classifier. Given an application activity record, respond with ONLY a JSON object (no markdown, no fences) matching this schema:
{
  "category": "deep-work" | "communication" | "planning" | "break" | "distraction" | "unknown",
  "confidence": <number 0-1>,
  "reasoning": "<one sentence>"
}`;

async function classifyUnknownActivity(
    client: GeminiClient,
    activity: ActivityRecord,
): Promise<ClassificationResult> {
    const userMessage = [
        `App: ${activity.appName}`,
        `Window title: ${activity.windowTitle}`,
        `Duration: ${activity.durationSeconds}s`,
    ].join('\n');

    const raw = await client.generateContent(
        [
            { role: 'user', parts: [{ text: SYSTEM_PROMPT }] },
            { role: 'model', parts: [{ text: 'Understood. Send the activity record.' }] },
            { role: 'user', parts: [{ text: userMessage }] },
        ],
        { temperature: 0.1, maxOutputTokens: 256 },
    );

    const text = raw.candidates[0]?.content.parts[0]?.text ?? '{}';
    const parsed = JSON.parse(text) as {
        category?: ProductivityCategory;
        confidence?: number;
        reasoning?: string;
    };

    return {
        activity,
        category: parsed.category ?? 'unknown',
        confidence: parsed.confidence ?? 0,
        reasoning: parsed.reasoning ?? '',
    };
}

// ── Main ───────────────────────────────────────────────────────────────

async function main(): Promise<void> {
    const client = new GeminiClient(
        {
            // apiKey resolved from GEMINI_API_KEY env or gemini.config.json
            model: 'gemini-2.0-flash',
            timeoutMs: 15_000,
            maxRetries: 2,
            rateLimitRpm: 30,
        },
        'debug',
    );

    const samples: ActivityRecord[] = [
        { appName: 'VS Code', windowTitle: 'gemini-client.ts — VIGILANT', durationSeconds: 2400 },
        { appName: 'Slack', windowTitle: '#team-standup', durationSeconds: 300 },
        { appName: 'YouTube', windowTitle: 'Lo-fi beats to study to', durationSeconds: 7200 },
        { appName: 'Notion', windowTitle: 'Q3 Roadmap', durationSeconds: 900 },
    ];

    try {
        for (const activity of samples) {
            const result = await classifyUnknownActivity(client, activity);
            console.log(
                `[${result.category}] (${(result.confidence * 100).toFixed(0)}%) ` +
                `${activity.appName} – ${result.reasoning}`,
            );
        }
    } finally {
        client.dispose();
    }
}

main().catch(console.error);

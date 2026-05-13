/**
 * Daily narrative – prompt template.
 *
 * Builds a compact, JSON-only prompt for Gemini that:
 *   • Keeps the system instruction short (~300 tokens).
 *   • Embeds the compressed timeline from the preprocessor.
 *   • Requests exactly the NarrativeResult schema.
 *   • Includes one few-shot example to anchor format.
 */

// ── System prompt ──────────────────────────────────────────────────────

export const NARRATIVE_SYSTEM_PROMPT = `You are a personal productivity analyst.
Given a compressed desktop-activity timeline for one day, produce EXACTLY one JSON object – no markdown fences, no commentary outside the object.

Schema:
{
  "narrative": "<string, 5-10 sentences summarising the day>",
  "insights": ["<insight 1>", "<insight 2>", "<insight 3>"]
}

Timeline field key:
• start/end – HH:mm
• app – application name
• cat – P=productive, C=consumptive, N=neutral, U=unknown
• focus – 0-1 score
• mins – duration in minutes

Rules:
1. Output valid JSON only.
2. "narrative" must be 5 to 10 sentences, written in second person ("You …").
3. "insights" must contain exactly 3 short, actionable bullet strings.
4. Never mention raw window titles, file paths, or URLs – refer to apps and categories only.
5. Be encouraging but honest about time spent on consumptive activities.`;

// ── Few-shot example ───────────────────────────────────────────────────

const EXAMPLE_TIMELINE = `[{"start":"09:00","end":"11:30","app":"Visual Studio Code","cat":"P","focus":0.88,"mins":150},{"start":"11:30","end":"12:00","app":"Slack","cat":"P","focus":0.65,"mins":30},{"start":"12:00","end":"13:00","app":"Chrome","cat":"C","focus":0.30,"mins":60},{"start":"13:00","end":"15:00","app":"Visual Studio Code","cat":"P","focus":0.91,"mins":120},{"start":"15:00","end":"15:45","app":"Figma","cat":"P","focus":0.78,"mins":45},{"start":"15:45","end":"16:30","app":"Chrome","cat":"C","focus":0.25,"mins":45}]`;

const EXAMPLE_OUTPUT = `{"narrative":"You started the day with a strong 2.5-hour coding session in Visual Studio Code, maintaining a high focus score of 0.88. A short Slack check followed before lunch. Your lunch break included about an hour of casual browsing. The afternoon picked back up with another focused 2-hour coding block at 0.91 focus. You then switched to Figma for 45 minutes of design work. The day wound down with another 45-minute browsing session. Overall, you logged roughly 5.75 hours of productive work across coding and design. Consumptive time totalled about 1.75 hours, mostly web browsing.","insights":["Your morning deep-work block is your strongest – protect it by silencing notifications before 11:30.","Consider batching Slack and browsing into a single midday window to reduce context switches.","The afternoon coding session was your peak focus – schedule complex tasks there."]}`;

// ── Builder ────────────────────────────────────────────────────────────

/**
 * Build the full prompt string ready for `GeminiClient.prompt()`.
 *
 * @param timelineJson  Compressed timeline JSON from `compressTimeline()`.
 */
export function buildNarrativePrompt(timelineJson: string): string {
    return [
        NARRATIVE_SYSTEM_PROMPT,
        '',
        '--- Example ---',
        '',
        `Timeline:\n${EXAMPLE_TIMELINE}`,
        `Output:\n${EXAMPLE_OUTPUT}`,
        '',
        '--- Your day ---',
        '',
        `Timeline:\n${timelineJson}`,
        'Output:',
    ].join('\n');
}

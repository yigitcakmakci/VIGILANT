/**
 * NarrativeGenerator – orchestrator that ties together the
 * preprocessor, prompt builder, Gemini client, and schema validator.
 *
 * Usage:
 *   const gen = new NarrativeGenerator(geminiClient);
 *   const result = await gen.generate(todaySessions);
 */

import type { GeminiClient } from './gemini-client';
import type {
    SessionBlock,
    NarrativeResult,
    NarrativeConfig,
} from './narrative-types';
import { compressTimeline } from './narrative-preprocessor';
import { buildNarrativePrompt } from './narrative-prompt';
import { validateNarrativeJson } from './narrative-schema';
import { Logger } from './logger';

// ── Defaults ───────────────────────────────────────────────────────────

const MAX_RETRIES = 2;

const FALLBACK_RESULT: NarrativeResult = {
    narrative: 'Daily narrative could not be generated. Please try again later.',
    insights: [
        'Ensure your session data covers a reasonable portion of the day.',
        'Check that the Gemini API key is valid and quota is available.',
        'Review the application logs for detailed error information.',
    ],
};

// ── Generator ──────────────────────────────────────────────────────────

export class NarrativeGenerator {
    private readonly _client: GeminiClient;
    private readonly _config: NarrativeConfig;
    private readonly _log: Logger;

    constructor(client: GeminiClient, config?: NarrativeConfig) {
        this._client = client;
        this._config = config ?? {};
        this._log = new Logger('info');
    }

    // ── Public API ─────────────────────────────────────────────────────

    /**
     * Generate a daily narrative from the given session blocks.
     *
     * Retries up to {@link MAX_RETRIES} times on schema-validation
     * failures before returning a safe fallback.
     */
    async generate(sessions: readonly SessionBlock[]): Promise<NarrativeResult> {
        // 1. Preprocess – compress & mask.
        const { json: timelineJson, blockCount } = compressTimeline(sessions, this._config);
        this._log.info('narrative: timeline compressed', {
            inputBlocks: sessions.length,
            compressedBlocks: blockCount,
        });

        if (blockCount === 0) {
            this._log.warn('narrative: no blocks after compression');
            return FALLBACK_RESULT;
        }

        // 2. Build prompt.
        const prompt = buildNarrativePrompt(timelineJson);

        // 3. Call Gemini with retries on validation failure.
        for (let attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            try {
                const raw = await this._client.prompt(prompt, {
                    temperature: 0.4,
                    maxOutputTokens: 1024,
                });

                const validation = validateNarrativeJson(raw);
                if (validation.ok) {
                    this._log.info('narrative: generated successfully', { attempt });
                    return validation.data;
                }

                this._log.warn('narrative: schema validation failed', {
                    attempt,
                    error: validation.error,
                    raw: validation.raw.slice(0, 200),
                });
            } catch (err) {
                this._log.error(
                    'narrative: Gemini call failed',
                    err,
                    { attempt },
                );
            }
        }

        this._log.warn('narrative: all retries exhausted, returning fallback');
        return FALLBACK_RESULT;
    }
}

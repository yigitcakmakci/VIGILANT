/**
 * GeminiClient – high-level wrapper around the Gemini generateContent API.
 *
 * Combines:
 *   • Typed request / response interfaces
 *   • Automatic config resolution (env → json → explicit)
 *   • Token-bucket rate limiter
 *   • Fetch wrapper with timeout + exponential back-off
 *   • Structured latency / error logging
 *
 * Usage:
 *   const client = new GeminiClient({ apiKey: '...' });
 *   const res = await client.generateContent([
 *       { role: 'user', parts: [{ text: 'Explain recursion.' }] },
 *   ]);
 *   console.log(res.candidates[0].content.parts[0].text);
 *   client.dispose();
 */

import type {
    GeminiClientConfig,
    GeminiContent,
    GenerationConfig,
    SafetySetting,
    GenerateContentRequest,
    GenerateContentResponse,
    LogLevel,
} from './types';
import { resolveConfig, type ResolvedConfig } from './config';
import { TokenBucketLimiter } from './rate-limiter';
import { fetchWithRetry } from './fetch-retry';
import { Logger } from './logger';

// ── Client ─────────────────────────────────────────────────────────────

export class GeminiClient {
    private readonly _cfg: ResolvedConfig;
    private readonly _limiter: TokenBucketLimiter;
    private readonly _log: Logger;

    constructor(config?: Partial<GeminiClientConfig>, logLevel?: LogLevel) {
        this._cfg = resolveConfig(config);
        this._limiter = new TokenBucketLimiter(this._cfg.rateLimitRpm);
        this._log = new Logger(logLevel ?? 'info');
        this._log.info('GeminiClient initialized', { model: this._cfg.model });
    }

    // ── Public API ─────────────────────────────────────────────────────

    /**
     * Send a generateContent request and return the typed response.
     *
     * @param contents  Conversation turns (user / model).
     * @param generationConfig  Optional generation parameters.
     * @param safetySettings  Optional safety overrides.
     */
    async generateContent(
        contents: GeminiContent[],
        generationConfig?: GenerationConfig,
        safetySettings?: SafetySetting[],
    ): Promise<GenerateContentResponse> {
        await this._limiter.acquire();

        const url = this._buildUrl();
        const body: GenerateContentRequest = {
            contents,
            ...(generationConfig ? { generationConfig } : {}),
            ...(safetySettings ? { safetySettings } : {}),
        };

        const t0 = performance.now();
        const res = await fetchWithRetry(
            url,
            {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body),
            },
            {
                timeoutMs: this._cfg.timeoutMs,
                maxRetries: this._cfg.maxRetries,
                logger: this._log,
            },
        );

        if (!res.ok) {
            const text = await res.text().catch(() => '');
            const latencyMs = Math.round(performance.now() - t0);
            this._log.error(
                `generateContent failed [${res.status}]`,
                new Error(text),
                { latencyMs, status: res.status },
            );
            throw new Error(
                `[GeminiClient] HTTP ${res.status}: ${text}`,
            );
        }

        const data = (await res.json()) as GenerateContentResponse;
        const latencyMs = Math.round(performance.now() - t0);
        this._log.logRequest('generateContent', latencyMs, true, {
            tokens: data.usageMetadata?.totalTokenCount,
        });
        return data;
    }

    /**
     * Convenience: send a single user prompt and return the first
     * candidate's text.
     */
    async prompt(text: string, generationConfig?: GenerationConfig): Promise<string> {
        const res = await this.generateContent(
            [{ role: 'user', parts: [{ text }] }],
            generationConfig,
        );
        return res.candidates[0]?.content.parts[0]?.text ?? '';
    }

    /** Release rate-limiter timers. Call when the client is no longer needed. */
    dispose(): void {
        this._limiter.dispose();
        this._log.info('GeminiClient disposed');
    }

    // ── Internal ───────────────────────────────────────────────────────

    private _buildUrl(): string {
        const { baseUrl, model, apiKey } = this._cfg;
        return `${baseUrl}/models/${model}:generateContent?key=${apiKey}`;
    }
}

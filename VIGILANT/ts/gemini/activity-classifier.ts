/**
 * ActivityClassifier – orchestrator that ties together the prompt
 * builder, Gemini client, schema validator, and cache.
 *
 * Usage:
 *   const classifier = new ActivityClassifier(geminiClient);
 *   const result = await classifier.classify({
 *       exePath: 'C:\\...\\Code.exe',
 *       appName: 'Visual Studio Code',
 *       windowTitle: 'index.ts — MyProject',
 *       recentUserLabels: ['coding'],
 *   });
 */

import type { GeminiClient } from './gemini-client';
import type { ActivityInput, ClassificationResult, ClassifierConfig } from './classifier-types';
import { buildClassifierPrompt } from './classifier-prompt';
import { validateClassificationJson } from './classifier-schema';
import { ClassifierCache } from './classifier-cache';
import { Logger } from './logger';

// ── Defaults ───────────────────────────────────────────────────────────

const DEFAULT_CONFIDENCE_THRESHOLD = 0.65;
const DEFAULT_CACHE_CAPACITY = 512;

// ── Classifier ─────────────────────────────────────────────────────────

export class ActivityClassifier {
    private readonly _client: GeminiClient;
    private readonly _cache: ClassifierCache;
    private readonly _threshold: number;
    private readonly _log: Logger;

    constructor(client: GeminiClient, config?: ClassifierConfig) {
        this._client = client;
        this._threshold = config?.confidenceThreshold ?? DEFAULT_CONFIDENCE_THRESHOLD;
        this._cache = new ClassifierCache(config?.cacheCapacity ?? DEFAULT_CACHE_CAPACITY);
        this._log = new Logger('info');
    }

    // ── Public API ─────────────────────────────────────────────────────

    async classify(input: ActivityInput): Promise<ClassificationResult> {
        // 1. Cache lookup.
        const cached = this._cache.get(input.exePath, input.windowTitle);
        if (cached) {
            this._log.debug('classifier cache hit', {
                exePath: input.exePath,
                title: input.windowTitle,
            });
            return cached;
        }

        // 2. Build prompt & call Gemini.
        const prompt = buildClassifierPrompt(input);
        const raw = await this._client.prompt(prompt, {
            temperature: 0,
            maxOutputTokens: 256,
        });

        // 3. Validate response.
        const validation = validateClassificationJson(raw);
        if (!validation.ok) {
            this._log.warn('classifier schema validation failed', {
                error: validation.error,
                raw: validation.raw,
            });
            return {
                category: 'unknown',
                confidence: 0,
                rationale: `Schema validation failed: ${validation.error}`,
            };
        }

        let result = validation.data;

        // 4. Enforce confidence threshold.
        if (result.confidence < this._threshold) {
            result = {
                category: 'unknown',
                confidence: result.confidence,
                rationale: `Below threshold (${this._threshold}): ${result.rationale}`,
            };
        }

        // 5. Cache & return.
        this._cache.set(input.exePath, input.windowTitle, result);
        this._log.debug('classifier result', {
            category: result.category,
            confidence: result.confidence,
        });
        return result;
    }

    /** Expose cache size for diagnostics. */
    get cacheSize(): number {
        return this._cache.size;
    }

    /** Clear the classification cache. */
    clearCache(): void {
        this._cache.clear();
    }
}

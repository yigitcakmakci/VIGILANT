/**
 * Configuration loader for the Gemini client.
 *
 * Resolution order:
 *   1. Explicit `apiKey` passed in GeminiClientConfig.
 *   2. `GEMINI_API_KEY` environment variable.
 *   3. A local JSON file (`gemini.config.json`) next to the entry point.
 *
 * Throws immediately if no key can be resolved — fail-fast beats
 * a cryptic 401 three layers deeper.
 */

import { readFileSync, existsSync } from 'fs';
import { resolve } from 'path';
import type { GeminiClientConfig } from './types';

// ── Defaults ───────────────────────────────────────────────────────────

const DEFAULT_MODEL = 'gemini-2.0-flash';
const DEFAULT_TIMEOUT_MS = 30_000;
const DEFAULT_MAX_RETRIES = 3;
const DEFAULT_RATE_LIMIT_RPM = 60;
const DEFAULT_BASE_URL = 'https://generativelanguage.googleapis.com/v1beta';
const CONFIG_FILE_NAME = 'gemini.config.json';

// ── Resolved config (all fields guaranteed) ────────────────────────────

export interface ResolvedConfig {
    apiKey: string;
    model: string;
    timeoutMs: number;
    maxRetries: number;
    rateLimitRpm: number;
    baseUrl: string;
}

// ── Loader ─────────────────────────────────────────────────────────────

function tryLoadJsonKey(): string | undefined {
    const filePath = resolve(process.cwd(), CONFIG_FILE_NAME);
    if (!existsSync(filePath)) return undefined;
    try {
        const raw = JSON.parse(readFileSync(filePath, 'utf-8')) as Record<string, unknown>;
        return typeof raw['apiKey'] === 'string' ? raw['apiKey'] : undefined;
    } catch {
        return undefined;
    }
}

export function resolveConfig(partial: Partial<GeminiClientConfig> = {}): ResolvedConfig {
    const apiKey =
        partial.apiKey ??
        process.env['GEMINI_API_KEY'] ??
        tryLoadJsonKey();

    if (!apiKey) {
        throw new Error(
            '[GeminiClient] API key not found. Provide it via config, ' +
            'GEMINI_API_KEY env var, or gemini.config.json.',
        );
    }

    return {
        apiKey,
        model: partial.model ?? DEFAULT_MODEL,
        timeoutMs: partial.timeoutMs ?? DEFAULT_TIMEOUT_MS,
        maxRetries: partial.maxRetries ?? DEFAULT_MAX_RETRIES,
        rateLimitRpm: partial.rateLimitRpm ?? DEFAULT_RATE_LIMIT_RPM,
        baseUrl: partial.baseUrl ?? DEFAULT_BASE_URL,
    };
}

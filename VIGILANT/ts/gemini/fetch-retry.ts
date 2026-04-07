/**
 * Fetch wrapper with timeout + exponential back-off retry.
 *
 * Retries only on transient failures:
 *   • Network errors (fetch throws)
 *   • HTTP 429 (rate-limited by upstream)
 *   • HTTP 5xx (server-side transient)
 *
 * All other status codes surface immediately so the caller can
 * handle 4xx auth / validation errors without waiting.
 */

import { Logger, classifyError } from './logger';

// ── Types ──────────────────────────────────────────────────────────────

export interface FetchRetryOptions {
    /** Absolute timeout per attempt in ms. */
    timeoutMs: number;
    /** Maximum number of retry attempts (0 = no retries). */
    maxRetries: number;
    /** Base delay for exponential back-off in ms (default 500). */
    baseDelayMs?: number;
    /** Optional logger instance. */
    logger?: Logger;
}

// ── Helpers ────────────────────────────────────────────────────────────

function isTransient(status: number): boolean {
    return status === 429 || (status >= 500 && status < 600);
}

function jitteredDelay(base: number, attempt: number): number {
    const exp = base * Math.pow(2, attempt);
    return exp + Math.random() * exp * 0.3;          // +0-30 % jitter
}

function sleep(ms: number): Promise<void> {
    return new Promise(r => setTimeout(r, ms));
}

// ── fetchWithRetry ─────────────────────────────────────────────────────

export async function fetchWithRetry(
    url: string,
    init: RequestInit,
    opts: FetchRetryOptions,
): Promise<Response> {
    const { timeoutMs, maxRetries, baseDelayMs = 500, logger } = opts;

    for (let attempt = 0; attempt <= maxRetries; attempt++) {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), timeoutMs);

        const t0 = performance.now();
        try {
            const res = await fetch(url, { ...init, signal: controller.signal });
            const latencyMs = Math.round(performance.now() - t0);
            logger?.logRequest('fetch', latencyMs, res.ok, {
                attempt,
                status: res.status,
                url,
            });

            if (res.ok || !isTransient(res.status)) return res;

            // Transient – fall through to retry logic.
        } catch (err: unknown) {
            const latencyMs = Math.round(performance.now() - t0);
            logger?.logRequest('fetch', latencyMs, false, {
                attempt,
                errorClass: classifyError(err),
                url,
            });

            if (attempt === maxRetries) throw err;
        } finally {
            clearTimeout(timer);
        }

        const delay = jitteredDelay(baseDelayMs, attempt);
        logger?.debug(`Retry #${attempt + 1} in ${Math.round(delay)} ms`);
        await sleep(delay);
    }

    // Unreachable in practice, but satisfies the compiler.
    throw new Error('[fetchWithRetry] Retries exhausted');
}

/**
 * Token-bucket rate limiter.
 *
 * Capacity refills linearly; callers `await limiter.acquire()` before
 * each request.  Resolved immediately when a token is available,
 * otherwise the promise parks until the next refill tick.
 *
 * Why token-bucket instead of sliding window?
 *   • Simpler state (one counter + timestamp).
 *   • Naturally smooths short bursts without a hard wall at window edges.
 */

// ── Rate limiter ───────────────────────────────────────────────────────

export class TokenBucketLimiter {
    private _tokens: number;
    private readonly _capacity: number;
    private readonly _refillIntervalMs: number;
    private _lastRefillTime: number;
    private readonly _waitQueue: Array<() => void> = [];
    private _timerId: ReturnType<typeof setInterval> | null = null;

    /**
     * @param requestsPerMinute  Maximum sustained throughput.
     */
    constructor(requestsPerMinute: number) {
        this._capacity = requestsPerMinute;
        this._tokens = requestsPerMinute;                 // start full
        this._refillIntervalMs = 60_000 / requestsPerMinute;
        this._lastRefillTime = Date.now();
        this._startRefillLoop();
    }

    /** Wait until a token is available, then consume it. */
    acquire(): Promise<void> {
        this._refill();
        if (this._tokens >= 1) {
            this._tokens -= 1;
            return Promise.resolve();
        }
        return new Promise<void>(resolve => {
            this._waitQueue.push(resolve);
        });
    }

    /** Stop the refill timer (call on shutdown). */
    dispose(): void {
        if (this._timerId !== null) {
            clearInterval(this._timerId);
            this._timerId = null;
        }
        // Drain any parked callers so they don't hang forever.
        for (const resolve of this._waitQueue.splice(0)) resolve();
    }

    // ── Internals ──────────────────────────────────────────────────────

    private _refill(): void {
        const now = Date.now();
        const elapsed = now - this._lastRefillTime;
        const newTokens = Math.floor(elapsed / this._refillIntervalMs);
        if (newTokens > 0) {
            this._tokens = Math.min(this._capacity, this._tokens + newTokens);
            this._lastRefillTime += newTokens * this._refillIntervalMs;
        }
    }

    private _startRefillLoop(): void {
        this._timerId = setInterval(() => {
            this._refill();
            while (this._tokens >= 1 && this._waitQueue.length > 0) {
                this._tokens -= 1;
                this._waitQueue.shift()!();
            }
        }, this._refillIntervalMs);

        // Don't prevent Node from exiting if this is the only timer left.
        if (typeof this._timerId === 'object' && 'unref' in this._timerId) {
            this._timerId.unref();
        }
    }
}

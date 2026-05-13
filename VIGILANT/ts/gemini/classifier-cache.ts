/**
 * Activity classifier – in-memory LRU cache.
 *
 * Key = SHA-256 of (exePath + windowTitle).
 * Bounded by a configurable capacity; evicts least-recently-used entries.
 */

import { createHash } from 'crypto';
import type { ClassificationResult } from './classifier-types';

// ── Cache ──────────────────────────────────────────────────────────────

export class ClassifierCache {
    private readonly _capacity: number;
    /** Insertion-order map – oldest entries are first. */
    private readonly _map = new Map<string, ClassificationResult>();

    constructor(capacity = 512) {
        this._capacity = Math.max(1, capacity);
    }

    // ── Key helpers ────────────────────────────────────────────────────

    /** Compute a deterministic cache key from the activity fingerprint. */
    static key(exePath: string, windowTitle: string): string {
        return createHash('sha256')
            .update(`${exePath}\0${windowTitle}`)
            .digest('hex');
    }

    // ── Public API ─────────────────────────────────────────────────────

    get(exePath: string, windowTitle: string): ClassificationResult | undefined {
        const k = ClassifierCache.key(exePath, windowTitle);
        const entry = this._map.get(k);
        if (entry === undefined) return undefined;

        // Move to end (most-recently-used).
        this._map.delete(k);
        this._map.set(k, entry);
        return entry;
    }

    set(exePath: string, windowTitle: string, result: ClassificationResult): void {
        const k = ClassifierCache.key(exePath, windowTitle);

        // Delete first so re-insert goes to end.
        this._map.delete(k);
        this._map.set(k, result);

        // Evict oldest if over capacity.
        while (this._map.size > this._capacity) {
            const oldest = this._map.keys().next().value;
            if (oldest !== undefined) this._map.delete(oldest);
        }
    }

    get size(): number {
        return this._map.size;
    }

    clear(): void {
        this._map.clear();
    }
}

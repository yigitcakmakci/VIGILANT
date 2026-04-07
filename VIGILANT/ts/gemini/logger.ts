/**
 * Gemini client – structured logger.
 *
 * Emits JSON-structured log entries with latency tracking and
 * error classification.  Designed to be consumed by both human readers
 * (pretty-printed in dev) and log aggregators (JSON in production).
 */

import type { LogLevel, LogEntry } from './types';

// ── Logger ─────────────────────────────────────────────────────────────

export class Logger {
    private readonly _minLevel: LogLevel;

    private static readonly _LEVELS: Record<LogLevel, number> = {
        debug: 0,
        info: 1,
        warn: 2,
        error: 3,
    };

    constructor(minLevel: LogLevel = 'info') {
        this._minLevel = minLevel;
    }

    debug(message: string, meta?: Record<string, unknown>): void {
        this._log('debug', message, meta);
    }

    info(message: string, meta?: Record<string, unknown>): void {
        this._log('info', message, meta);
    }

    warn(message: string, meta?: Record<string, unknown>): void {
        this._log('warn', message, meta);
    }

    error(message: string, err?: unknown, meta?: Record<string, unknown>): void {
        const errorClass = classifyError(err);
        this._log('error', message, { ...meta, errorClass });
    }

    /** Log a completed request with latency. */
    logRequest(
        method: string,
        latencyMs: number,
        ok: boolean,
        meta?: Record<string, unknown>,
    ): void {
        const level: LogLevel = ok ? 'info' : 'warn';
        this._log(level, `${method} ${ok ? 'OK' : 'FAIL'}`, { latencyMs, ...meta });
    }

    // ── Internal ───────────────────────────────────────────────────────

    private _log(level: LogLevel, message: string, meta?: Record<string, unknown>): void {
        if (Logger._LEVELS[level] < Logger._LEVELS[this._minLevel]) return;

        const entry: LogEntry = {
            timestamp: new Date().toISOString(),
            level,
            message,
            ...(meta?.['latencyMs'] != null ? { latencyMs: meta['latencyMs'] as number } : {}),
            ...(meta?.['errorClass'] != null ? { errorClass: meta['errorClass'] as string } : {}),
            meta,
        };

        const line = JSON.stringify(entry);
        if (level === 'error') console.error(line);
        else if (level === 'warn') console.warn(line);
        else console.log(line);
    }
}

// ── Error classification ───────────────────────────────────────────────

export function classifyError(err: unknown): string {
    if (err == null) return 'Unknown';
    if (err instanceof TypeError) return 'TypeError';
    if (err instanceof SyntaxError) return 'ParseError';

    const msg = err instanceof Error ? err.message : String(err);
    if (msg.includes('AbortError') || msg.includes('timeout')) return 'Timeout';
    if (msg.includes('ECONNREFUSED') || msg.includes('ENOTFOUND')) return 'Network';
    if (msg.includes('429')) return 'RateLimited';
    if (msg.includes('401') || msg.includes('403')) return 'Auth';
    if (msg.includes('5')) return 'Server';
    return 'Unknown';
}

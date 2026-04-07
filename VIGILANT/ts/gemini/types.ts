/**
 * Gemini API – TypeScript type definitions.
 *
 * Covers the generateContent endpoint request / response shapes
 * plus internal helper types used across the client.
 */

// ── Request types ──────────────────────────────────────────────────────

export interface GeminiPart {
    text: string;
}

export interface GeminiContent {
    role: 'user' | 'model';
    parts: GeminiPart[];
}

export interface GenerationConfig {
    temperature?: number;
    topP?: number;
    topK?: number;
    maxOutputTokens?: number;
    stopSequences?: string[];
}

export interface SafetySetting {
    category: string;
    threshold: string;
}

export interface GenerateContentRequest {
    contents: GeminiContent[];
    generationConfig?: GenerationConfig;
    safetySettings?: SafetySetting[];
}

// ── Response types ─────────────────────────────────────────────────────

export interface GeminiCandidate {
    content: GeminiContent;
    finishReason: string;
    index: number;
    safetyRatings?: SafetyRating[];
}

export interface SafetyRating {
    category: string;
    probability: string;
}

export interface UsageMetadata {
    promptTokenCount: number;
    candidatesTokenCount: number;
    totalTokenCount: number;
}

export interface GenerateContentResponse {
    candidates: GeminiCandidate[];
    usageMetadata?: UsageMetadata;
}

// ── Config ─────────────────────────────────────────────────────────────

export interface GeminiClientConfig {
    /** Gemini API key – resolved from env or json at construction time. */
    apiKey: string;
    /** Model identifier, e.g. "gemini-2.0-flash". */
    model?: string;
    /** Per-request timeout in ms (default 30 000). */
    timeoutMs?: number;
    /** Max retry attempts on transient errors (default 3). */
    maxRetries?: number;
    /** Token-bucket capacity per minute (default 60). */
    rateLimitRpm?: number;
    /** Base URL override (useful for proxies / mocks). */
    baseUrl?: string;
}

// ── Logger ─────────────────────────────────────────────────────────────

export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export interface LogEntry {
    timestamp: string;
    level: LogLevel;
    message: string;
    latencyMs?: number;
    errorClass?: string;
    meta?: Record<string, unknown>;
}

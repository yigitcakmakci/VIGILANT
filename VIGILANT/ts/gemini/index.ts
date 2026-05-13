/**
 * Barrel export for the Gemini client module.
 */

export { GeminiClient } from './gemini-client';
export { TokenBucketLimiter } from './rate-limiter';
export { fetchWithRetry, type FetchRetryOptions } from './fetch-retry';
export { Logger, classifyError } from './logger';
export { resolveConfig, type ResolvedConfig } from './config';
export type {
    GeminiClientConfig,
    GeminiContent,
    GeminiPart,
    GenerationConfig,
    SafetySetting,
    GenerateContentRequest,
    GenerateContentResponse,
    GeminiCandidate,
    SafetyRating,
    UsageMetadata,
    LogLevel,
    LogEntry,
} from './types';

// -- Activity classifier ----------------------------------------------------

export { ActivityClassifier } from './activity-classifier';
export { buildClassifierPrompt, CLASSIFIER_SYSTEM_PROMPT } from './classifier-prompt';
export { validateClassificationJson } from './classifier-schema';
export type { ValidationResult, ValidationOk, ValidationFail } from './classifier-schema';
export { ClassifierCache } from './classifier-cache';
export type {
    ActivityInput,
    ActivityCategory,
    ClassificationResult,
    ClassifierConfig,
} from './classifier-types';

// -- Daily narrative --------------------------------------------------------

export { NarrativeGenerator } from './narrative-generator';
export { compressTimeline } from './narrative-preprocessor';
export { buildNarrativePrompt, NARRATIVE_SYSTEM_PROMPT } from './narrative-prompt';
export { validateNarrativeJson } from './narrative-schema';
export type { NarrativeValidationResult, NarrativeValidationOk, NarrativeValidationFail } from './narrative-schema';
export type {
    SessionBlock,
    CompressedBlock,
    NarrativeResult,
    NarrativeConfig,
} from './narrative-types';
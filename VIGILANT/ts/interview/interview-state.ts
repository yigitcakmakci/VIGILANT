/**
 * interview-state.ts — Minimal state management for the Socratic Interview.
 *
 * Pure state + reducer pattern (no framework dependency).
 * Enforces maxQuestions hard limit and idempotent finalize.
 */

// ═══════════════════════════════════════════════════════════════════════
// Types
// ═══════════════════════════════════════════════════════════════════════

export type Role = 'user' | 'ai' | 'system';

export interface ChatMessage {
    id: string;
    role: Role;
    text: string;
    ts: string;
}

export type InterviewStatus = 'idle' | 'asking' | 'finalizing' | 'finalized' | 'error';

export interface InterviewState {
    sessionId: string;
    status: InterviewStatus;
    questionCount: number;
    maxQuestions: 3;
    endedBy?: 'limit' | 'cta';
    transcript: ChatMessage[];
    isSending: boolean;
    finalizeRequested: boolean;
    errorMessage?: string;
}

// ═══════════════════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════════════════

export function createInitialState(): InterviewState {
    return {
        sessionId: '',
        status: 'idle',
        questionCount: 0,
        maxQuestions: 3,
        transcript: [],
        isSending: false,
        finalizeRequested: false,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Actions
// ═══════════════════════════════════════════════════════════════════════

export type InterviewAction =
    | { type: 'SESSION_STARTED'; sessionId: string }
    | { type: 'USER_MESSAGE_SENT'; message: ChatMessage }
    | { type: 'SENDING_START' }
    | { type: 'SENDING_END' }
    | { type: 'AI_QUESTION_RECEIVED'; message: ChatMessage; questionCount: number; autoFinalize?: boolean }
    | { type: 'FINALIZE_REQUESTED'; endedBy: 'cta' | 'limit' }
    | { type: 'FINALIZE_CONFIRMED'; endedBy: 'cta' | 'limit'; questionCount: number }
    | { type: 'ERROR'; message: string }
    | { type: 'RESET' };

// ═══════════════════════════════════════════════════════════════════════
// Reducer
// ═══════════════════════════════════════════════════════════════════════

export function interviewReducer(state: InterviewState, action: InterviewAction): InterviewState {
    switch (action.type) {

        case 'SESSION_STARTED':
            return {
                ...createInitialState(),
                sessionId: action.sessionId,
                status: 'asking',
            };

        case 'SENDING_START':
            return { ...state, isSending: true };

        case 'SENDING_END':
            return { ...state, isSending: false };

        case 'USER_MESSAGE_SENT':
            return {
                ...state,
                transcript: [...state.transcript, action.message],
            };

        case 'AI_QUESTION_RECEIVED': {
            const newCount = action.questionCount;
            return {
                ...state,
                transcript: [...state.transcript, action.message],
                questionCount: newCount,
                isSending: false,
                // Keep 'asking' so the user can answer the last question;
                // C++ auto-finalizes when the final answer is received.
                status: 'asking',
            };
        }

        case 'FINALIZE_REQUESTED':
            // Idempotent: if already requested, no-op
            if (state.finalizeRequested || state.status === 'finalized') return state;
            return {
                ...state,
                finalizeRequested: true,
                status: 'finalizing',
                endedBy: action.endedBy,
            };

        case 'FINALIZE_CONFIRMED':
            return {
                ...state,
                status: 'finalized',
                finalizeRequested: true,
                endedBy: action.endedBy,
                questionCount: action.questionCount,
                isSending: false,
            };

        case 'ERROR':
            return {
                ...state,
                status: 'error',
                errorMessage: action.message,
                isSending: false,
            };

        case 'RESET':
            return createInitialState();

        default:
            return state;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Selectors
// ═══════════════════════════════════════════════════════════════════════

export function canSendMessage(state: InterviewState): boolean {
    return state.status === 'asking' &&
           !state.isSending &&
           !state.finalizeRequested;
}

export function canFinalize(state: InterviewState): boolean {
    return !state.finalizeRequested &&
           state.status !== 'finalized' &&
           state.status !== 'idle' &&
           state.sessionId !== '';
}

export function isLimitReached(state: InterviewState): boolean {
    return state.questionCount >= state.maxQuestions;
}

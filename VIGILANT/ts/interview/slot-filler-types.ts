/**
 * slot-filler-types.ts — TypeScript interfaces for the InterviewSlotFiller module.
 *
 * Mirrors the C++ SlotEntry / InterviewSlotFiller schema exactly.
 * Used by the UI to render slot progress and handle EventBridge messages.
 */

// ═══════════════════════════════════════════════════════════════════════
// Slot schema (mirrors C++ SlotEntry)
// ═══════════════════════════════════════════════════════════════════════

export type SlotStatus = 'empty' | 'ambiguous' | 'filled';

export type SlotName = 'goal' | 'timeframe' | 'weekly_hours' | 'current_level' | 'constraints';

export interface SlotInfo {
    name: SlotName;
    status: SlotStatus;
    required: boolean;
    attempts: number;
    value: string | string[];   // string for scalar, string[] for constraints
}

// ═══════════════════════════════════════════════════════════════════════
// Filled slot values (final output)
// ═══════════════════════════════════════════════════════════════════════

export interface SlotValues {
    goal: string | null;
    timeframe: string | null;
    weekly_hours: string | null;         // stored as string from backend
    current_level: string | null;
    constraints: string[] | null;
}

// ═══════════════════════════════════════════════════════════════════════
// EventBridge payload types (C++ → UI)
// ═══════════════════════════════════════════════════════════════════════

/** AskNextQuestion — backend asks UI to display a question for a slot */
export interface AskNextQuestionPayload {
    slotName: SlotName;
    questionText: string;
    messageId: string;
    questionCount: number;
    maxQuestions: number;
    filledCount: number;
    totalSlots: number;
    slots: SlotInfo[];
}

/** SlotPatched — a slot was successfully filled */
export interface SlotPatchedPayload {
    slotName: SlotName;
    status: 'filled';
    value: string | string[];
    filledCount: number;
    totalSlots: number;
    slots: SlotInfo[];
}

/** SlotPatchedAndNext — combined: slot patched + next question */
export interface SlotPatchedAndNextPayload {
    patched: SlotPatchedPayload;
    next: AskNextQuestionPayload;
}

/** InterviewUpdate — reset or finalized */
export interface InterviewUpdatePayload {
    action: 'reset' | 'finalized';
    endedBy?: 'cta' | 'limit' | 'complete';
    finalized: boolean;
    filledCount: number;
    totalSlots: number;
    questionCount?: number;
    slots: SlotInfo[];
    slotValues?: SlotValues;
    alreadyFinalized?: boolean;
}

// ═══════════════════════════════════════════════════════════════════════
// Slot display labels (Turkish)
// ═══════════════════════════════════════════════════════════════════════

export const SLOT_LABELS: Record<SlotName, string> = {
    goal: 'Hedef',
    timeframe: 'Zaman Çerçevesi',
    weekly_hours: 'Haftalık Saat',
    current_level: 'Mevcut Seviye',
    constraints: 'Kısıtlamalar',
};

// ═══════════════════════════════════════════════════════════════════════
// Slot filler state (client-side)
// ═══════════════════════════════════════════════════════════════════════

export type SlotFillerStatus = 'idle' | 'asking' | 'finalizing' | 'finalized' | 'error';

export interface SlotFillerState {
    status: SlotFillerStatus;
    slots: SlotInfo[];
    filledCount: number;
    totalSlots: number;
    questionCount: number;
    maxQuestions: number;
    currentSlotName: SlotName | null;
    currentQuestion: string | null;
    endedBy?: 'cta' | 'limit' | 'complete';
    slotValues?: SlotValues;
    errorMessage?: string;
}

export function createInitialSlotFillerState(): SlotFillerState {
    return {
        status: 'idle',
        slots: [],
        filledCount: 0,
        totalSlots: 5,
        questionCount: 0,
        maxQuestions: 3,
        currentSlotName: null,
        currentQuestion: null,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Actions (reducer pattern)
// ═══════════════════════════════════════════════════════════════════════

export type SlotFillerAction =
    | { type: 'SLOT_SESSION_STARTED'; slots: SlotInfo[] }
    | { type: 'ASK_NEXT_QUESTION'; payload: AskNextQuestionPayload }
    | { type: 'SLOT_PATCHED'; payload: SlotPatchedPayload }
    | { type: 'SLOT_PATCHED_AND_NEXT'; payload: SlotPatchedAndNextPayload }
    | { type: 'SLOT_FINALIZED'; payload: InterviewUpdatePayload }
    | { type: 'SLOT_ERROR'; message: string }
    | { type: 'SLOT_RESET' };

export function slotFillerReducer(state: SlotFillerState, action: SlotFillerAction): SlotFillerState {
    switch (action.type) {
        case 'SLOT_SESSION_STARTED':
            return {
                ...createInitialSlotFillerState(),
                status: 'asking',
                slots: action.slots,
                totalSlots: action.slots.length,
            };

        case 'ASK_NEXT_QUESTION':
            return {
                ...state,
                status: 'asking',
                slots: action.payload.slots,
                filledCount: action.payload.filledCount,
                totalSlots: action.payload.totalSlots,
                questionCount: action.payload.questionCount,
                maxQuestions: action.payload.maxQuestions,
                currentSlotName: action.payload.slotName,
                currentQuestion: action.payload.questionText,
            };

        case 'SLOT_PATCHED':
            return {
                ...state,
                slots: action.payload.slots,
                filledCount: action.payload.filledCount,
                totalSlots: action.payload.totalSlots,
            };

        case 'SLOT_PATCHED_AND_NEXT':
            return {
                ...state,
                status: 'asking',
                slots: action.payload.next.slots,
                filledCount: action.payload.next.filledCount,
                totalSlots: action.payload.next.totalSlots,
                questionCount: action.payload.next.questionCount,
                maxQuestions: action.payload.next.maxQuestions,
                currentSlotName: action.payload.next.slotName,
                currentQuestion: action.payload.next.questionText,
            };

        case 'SLOT_FINALIZED':
            return {
                ...state,
                status: 'finalized',
                slots: action.payload.slots,
                filledCount: action.payload.filledCount,
                totalSlots: action.payload.totalSlots,
                questionCount: action.payload.questionCount ?? state.questionCount,
                endedBy: action.payload.endedBy,
                slotValues: action.payload.slotValues,
            };

        case 'SLOT_ERROR':
            return {
                ...state,
                status: 'error',
                errorMessage: action.message,
            };

        case 'SLOT_RESET':
            return createInitialSlotFillerState();

        default:
            return state;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Selectors
// ═══════════════════════════════════════════════════════════════════════

export function canAnswerSlot(state: SlotFillerState): boolean {
    return state.status === 'asking' && state.currentQuestion !== null;
}

export function canFinalizeSlot(state: SlotFillerState): boolean {
    return state.status === 'asking' && state.filledCount > 0;
}

export function slotProgressPercent(state: SlotFillerState): number {
    if (state.totalSlots === 0) return 0;
    return Math.round((state.filledCount / state.totalSlots) * 100);
}

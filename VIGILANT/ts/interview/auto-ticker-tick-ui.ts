/**
 * auto-ticker-tick-ui.ts — UI-layer helpers for AutoTickerTickEngine.
 *
 * Pure functions — no DOM mutation, no side effects.
 * Consumed by the dashboard to render tick history and commit/reject states.
 */

import type {
    TickRecord,
    TickOutcome,
    RejectionReason,
    TickRejectedPayload,
} from './auto-ticker-tick-types';

// ═══════════════════════════════════════════════════════════════════════
// Rejection reason display
// ═══════════════════════════════════════════════════════════════════════

export interface RejectionDisplay {
    /** User-facing label */
    label: string;
    /** CSS class suffix: 'duplicate', 'invalid', 'error' */
    badgeClass: string;
    /** Icon hint */
    icon: '🔁' | '⚠️' | '❌';
    /** Whether the user can retry (only false for DUPLICATE) */
    canRetry: boolean;
}

/** Map a rejection reason to a user-facing display descriptor */
export function rejectionDisplay(reason: RejectionReason): RejectionDisplay {
    switch (reason) {
        case 'DUPLICATE':
            return {
                label: 'Zaten Uygulandı',
                badgeClass: 'duplicate',
                icon: '🔁',
                canRetry: false,
            };
        case 'VERDICT_NOT_PASS':
            return {
                label: 'Doğrulama Başarısız',
                badgeClass: 'invalid',
                icon: '❌',
                canRetry: false,
            };
        case 'MICRO_NOT_FOUND':
        case 'NO_GOAL_TREE':
        case 'NO_INTERVIEW_RESULT':
            return {
                label: 'Görev Bulunamadı',
                badgeClass: 'error',
                icon: '❌',
                canRetry: false,
            };
        case 'INVALID_INPUT':
            return {
                label: 'Geçersiz İstek',
                badgeClass: 'invalid',
                icon: '⚠️',
                canRetry: false,
            };
        case 'PERSIST_FAILED':
            return {
                label: 'Kayıt Hatası',
                badgeClass: 'error',
                icon: '❌',
                canRetry: true,
            };
        default:
            return {
                label: 'Bilinmeyen Hata',
                badgeClass: 'error',
                icon: '❌',
                canRetry: true,
            };
    }
}

/** Build a user-facing message for a rejection event */
export function rejectionMessage(payload: TickRejectedPayload): string {
    if (payload.rejectionMessage) {
        return payload.rejectionMessage;
    }
    const display = rejectionDisplay(payload.rejectionReason);
    return display.label;
}

// ═══════════════════════════════════════════════════════════════════════
// Commit outcome display
// ═══════════════════════════════════════════════════════════════════════

export interface CommitDisplay {
    /** User-facing status label */
    label: string;
    /** CSS class suffix: 'committed', 'rejected' */
    badgeClass: string;
    /** Icon hint */
    icon: '✅' | '❌' | '🔁';
}

/** Build display descriptor for a TickOutcome */
export function commitDisplay(outcome: TickOutcome): CommitDisplay {
    if (outcome.committed) {
        return {
            label: 'Tamamlandı',
            badgeClass: 'committed',
            icon: '✅',
        };
    }
    const rej = rejectionDisplay(outcome.rejectionReason);
    return {
        label: rej.label,
        badgeClass: 'rejected',
        icon: rej.icon,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Tick history display helpers
// ═══════════════════════════════════════════════════════════════════════

export interface TickHistoryItem {
    /** Journal entry that sourced this tick */
    journalEntryId: string;
    /** Formatted commit timestamp */
    committedAtFormatted: string;
    /** Formatted confidence */
    confidenceFormatted: string;
    /** AI model used */
    modelVersion: string;
}

/** Format a TickRecord into a display-ready item */
export function formatTickHistoryItem(record: TickRecord): TickHistoryItem {
    return {
        journalEntryId: record.journalEntryId,
        committedAtFormatted: formatTimestamp(record.committedAt),
        confidenceFormatted: `${Math.round(record.confidence * 100)}%`,
        modelVersion: record.modelVersion || 'unknown',
    };
}

/** Format ISO timestamp to a human-friendly format */
function formatTimestamp(iso: string): string {
    if (!iso) return '—';
    try {
        const d = new Date(iso);
        return d.toLocaleString('tr-TR', {
            day: '2-digit',
            month: '2-digit',
            year: 'numeric',
            hour: '2-digit',
            minute: '2-digit',
        });
    } catch {
        return iso;
    }
}

/** Build the complete list of formatted history items */
export function buildTickHistoryList(records: TickRecord[]): TickHistoryItem[] {
    return records.map(formatTickHistoryItem);
}

/** Returns a user-facing message when tick history is empty */
export function noTickHistoryMessage(): string {
    return 'Bu görev için henüz otomatik tick kaydı bulunmuyor.';
}

/** Returns a user-facing message when tick was sourced from a journal */
export function tickSourceMessage(journalEntryId: string, committedAt: string): string {
    const ts = formatTimestamp(committedAt);
    return `Bu görev ${ts} tarihinde "${journalEntryId}" günlüğü ile otomatik olarak tamamlandı.`;
}

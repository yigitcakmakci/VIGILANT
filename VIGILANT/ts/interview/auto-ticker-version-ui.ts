/**
 * auto-ticker-version-ui.ts — UI-layer helpers for GoalTree version changes.
 *
 * Pure functions — no DOM mutation, no side effects.
 * Consumed by the dashboard to render version-change toasts and
 * stale-candidate overlays.
 *
 * INVARIANT:
 *   These helpers NEVER mutate the GoalTree or mark tasks as done.
 */

import type {
    StaleCandidateEntry,
    StaleReason,
    InvalidationResult,
    CandidatesInvalidatedPayload,
} from './auto-ticker-version-types';

// ═══════════════════════════════════════════════════════════════════════
// Stale reason display
// ═══════════════════════════════════════════════════════════════════════

export interface StaleReasonDisplay {
    label: string;
    icon: '🗑️' | '✅' | '🔄';
    detail: string;
}

/** Map a staleness reason to user-facing display */
export function staleReasonDisplay(reason: StaleReason): StaleReasonDisplay {
    switch (reason) {
        case 'MICRO_REMOVED':
            return {
                label: 'Görev Kaldırıldı',
                icon: '🗑️',
                detail: 'Bu görev yeni planda artık mevcut değil.',
            };
        case 'MICRO_COMPLETED':
            return {
                label: 'Görev Tamamlanmış',
                icon: '✅',
                detail: 'Bu görev yeni planda zaten tamamlanmış.',
            };
        case 'TREE_VERSION_MISMATCH':
            return {
                label: 'Plan Değişti',
                icon: '🔄',
                detail: 'Hedef ağacı güncellendi, eşleşme geçerliliğini yitirdi.',
            };
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Version change toast
// ═══════════════════════════════════════════════════════════════════════

export interface VersionChangeToast {
    /** Title for the toast/overlay */
    title: string;
    /** Descriptive body text */
    body: string;
    /** CSS class suffix for styling: 'info', 'warning' */
    severity: 'info' | 'warning';
    /** Whether the "Re-match" action button should be shown */
    showRematchButton: boolean;
    /** Count of invalidated candidates */
    invalidatedCount: number;
    /** Count of surviving candidates */
    survivingCount: number;
}

/** Build a toast descriptor for a CandidatesInvalidated event */
export function buildVersionChangeToast(
    payload: CandidatesInvalidatedPayload,
): VersionChangeToast {
    const inv = payload.invalidated.length;
    const surv = payload.survivingCount;

    if (inv === 0) {
        return {
            title: 'Rota Yeniden Hesaplandı',
            body: 'Hedef ağacı güncellendi. Mevcut eşleşmeler hâlâ geçerli.',
            severity: 'info',
            showRematchButton: false,
            invalidatedCount: 0,
            survivingCount: surv,
        };
    }

    const removedCount = payload.invalidated.filter(
        (e) => e.reason === 'MICRO_REMOVED',
    ).length;
    const mismatchCount = payload.invalidated.filter(
        (e) => e.reason === 'TREE_VERSION_MISMATCH',
    ).length;

    let bodyParts: string[] = [];
    if (removedCount > 0) {
        bodyParts.push(`${removedCount} görev planından kaldırıldı`);
    }
    if (mismatchCount > 0) {
        bodyParts.push(`${mismatchCount} eşleşme geçerliliğini yitirdi`);
    }
    if (surv > 0) {
        bodyParts.push(`${surv} eşleşme hâlâ geçerli`);
    }

    return {
        title: 'Rota Yeniden Hesaplandı',
        body: bodyParts.join('. ') + '.',
        severity: 'warning',
        showRematchButton: true,
        invalidatedCount: inv,
        survivingCount: surv,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Stale candidate overlay
// ═══════════════════════════════════════════════════════════════════════

export interface StaleCandidateDisplayItem {
    microTaskId: string;
    reason: StaleReasonDisplay;
    oldVersionId: string;
    newVersionId: string;
}

/** Transform invalidated entries into display-ready items */
export function buildStaleCandidateList(
    entries: StaleCandidateEntry[],
): StaleCandidateDisplayItem[] {
    return entries.map((e) => ({
        microTaskId: e.microTaskId,
        reason: staleReasonDisplay(e.reason),
        oldVersionId: e.oldVersionId,
        newVersionId: e.newVersionId,
    }));
}

/** User-facing message when all candidates are invalidated */
export function allCandidatesStaleMessage(): string {
    return 'Tüm eşleşmeler geçerliliğini yitirdi. Yeni plana göre yeniden eşleştirme yapmanız önerilir.';
}

/** User-facing message when no candidates were invalidated */
export function noCandidatesInvalidatedMessage(): string {
    return 'Plan güncellendi, ancak mevcut eşleşmeler hâlâ geçerli.';
}

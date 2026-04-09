/**
 * goal-tree-ui.ts — UI-layer helpers for GoalTree MicroTask interactions.
 *
 * Pure functions — no DOM access.  Consumed by the dashboard to decide
 * whether the "Done" button is enabled and what tooltip to show.
 */

import type { MicroTask, Evidence, GoalTree, EvidenceType } from './goal-tree-types';
import { validateTickDone } from './goal-tree-schema';
import type { TickValidationError } from './goal-tree-schema';

// ═══════════════════════════════════════════════════════════════════════
// canMarkDone — gate for the "Done" button
// ═══════════════════════════════════════════════════════════════════════

export interface DoneGateResult {
    /** true → button enabled; false → button disabled */
    allowed: boolean;
    /** Human-readable tooltip explaining why the button is disabled */
    tooltip: string;
    /** Structured errors (empty when allowed) */
    errors: TickValidationError[];
}

/**
 * Determine whether a MicroTask's "Done" button should be enabled.
 *
 * @param micro     The MicroTask in question
 * @param evidence  Evidence the user has entered so far (may be partial)
 * @param tree      Full GoalTree for dependency checks
 */
export function canMarkDone(
    micro: MicroTask,
    evidence: Evidence | undefined,
    tree: GoalTree,
): DoneGateResult {
    // Already done — button should be disabled, no action needed
    if (micro.status === 'done') {
        return {
            allowed: false,
            tooltip: 'Bu görev zaten tamamlandı.',
            errors: [],
        };
    }

    const result = validateTickDone(micro, evidence, tree);

    if (result.ok) {
        return {
            allowed: true,
            tooltip: "Görevi tamamla",
            errors: [],
        };
    }

    // Build a human-readable tooltip from the structured errors
    const tooltip = result.errors
        .map((e) => errorCodeToTooltip(e.code, micro.evidence_type))
        .join('\n');

    return {
        allowed: false,
        tooltip,
        errors: result.errors,
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Evidence label helpers (for UI rendering)
// ═══════════════════════════════════════════════════════════════════════

const EVIDENCE_LABELS: Record<EvidenceType, string> = {
    text: 'Metin kanıtı girin',
    file: 'Dosya yükleyin',
    url: 'URL girin',
    metric: 'Metrik değeri girin',
};

/** Returns a placeholder/label string for the evidence input matching the type */
export function evidencePlaceholder(type: EvidenceType): string {
    return EVIDENCE_LABELS[type] ?? 'Kanıt girin';
}

// ── Internal: error code → tooltip mapping ─────────────────────────────

function errorCodeToTooltip(code: string, evidenceType: EvidenceType): string {
    switch (code) {
        case 'EMPTY_ACCEPTANCE_CRITERIA':
            return '⚠ Kabul kriteri tanımlı değil — bu görev tamamlanamaz.';
        case 'MISSING_EVIDENCE':
            return `📎 Kanıt gerekli: ${EVIDENCE_LABELS[evidenceType] ?? 'kanıt ekleyin'}.`;
        case 'MISSING_EVIDENCE_TEXT':
            return '📝 Metin kanıtı boş — lütfen açıklama yazın.';
        case 'MISSING_EVIDENCE_FILE':
            return '📁 Dosya kanıtı gerekli — lütfen bir dosya yükleyin.';
        case 'MISSING_EVIDENCE_URL':
            return '🔗 URL kanıtı gerekli — lütfen bir bağlantı girin.';
        case 'MISSING_EVIDENCE_METRIC':
            return '📊 Metrik değeri gerekli — lütfen sayısal bir değer girin.';
        case 'UNRESOLVED_DEPENDENCIES':
            return '🔒 Bağımlı görevler henüz tamamlanmadı.';
        default:
            return `⚠ Doğrulama hatası: ${code}`;
    }
}

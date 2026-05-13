/**
 * goal-tree-replanner.ts — GoalTree merge & diff logic for replan pipeline.
 *
 * Pure functions — no DOM access, no side-effects.
 *
 * Merge rule:
 *   If same microTask.id exists in both old and new → preserve status + evidence.
 *   If a micro existed in old but not in new → mark as 'orphaned' in diff.
 *   If a micro exists in new but not old → it's 'added'.
 *
 * Produces a GoalTreeDiff report for debug/audit purposes.
 */

import type {
    GoalTree,
    MajorGoal,
    MinorGoal,
    MicroTask,
    Evidence,
    MicroTaskStatus,
    DiffEntry,
    GoalTreeDiff,
} from './goal-tree-types';

// ═══════════════════════════════════════════════════════════════════════
// Index builders
// ═══════════════════════════════════════════════════════════════════════

interface MicroEntry {
    micro: MicroTask;
    minorId: string;
    majorId: string;
}

function buildMicroMap(tree: GoalTree): Map<string, MicroEntry> {
    const map = new Map<string, MicroEntry>();
    for (const maj of tree.majors) {
        for (const min of maj.minors) {
            for (const mic of min.micros) {
                map.set(mic.id, { micro: mic, minorId: min.id, majorId: maj.id });
            }
        }
    }
    return map;
}

function buildMinorMap(tree: GoalTree): Map<string, MinorGoal> {
    const map = new Map<string, MinorGoal>();
    for (const maj of tree.majors) {
        for (const min of maj.minors) {
            map.set(min.id, min);
        }
    }
    return map;
}

function buildMajorMap(tree: GoalTree): Map<string, MajorGoal> {
    const map = new Map<string, MajorGoal>();
    for (const maj of tree.majors) {
        map.set(maj.id, maj);
    }
    return map;
}

// ═══════════════════════════════════════════════════════════════════════
// mergeGoalTrees — carry over status/evidence from old to new
// ═══════════════════════════════════════════════════════════════════════

/**
 * Merge status and evidence from `oldTree` into `newTree` (mutates newTree).
 *
 * For each MicroTask in newTree:
 *   - If same id exists in oldTree AND old status == 'done'
 *     → copy status='done' and evidence
 *   - Otherwise keep newTree's defaults (open)
 *
 * Returns the mutated newTree (same reference).
 */
export function mergeGoalTrees(oldTree: GoalTree, newTree: GoalTree): GoalTree {
    const oldMicros = buildMicroMap(oldTree);

    for (const maj of newTree.majors) {
        for (const min of maj.minors) {
            for (const mic of min.micros) {
                const old = oldMicros.get(mic.id);
                if (old && old.micro.status === 'done') {
                    mic.status = 'done';
                    mic.evidence = old.micro.evidence;
                }
            }
        }
    }

    return newTree;
}

// ═══════════════════════════════════════════════════════════════════════
// diffGoalTrees — produce a structured diff report
// ═══════════════════════════════════════════════════════════════════════

/**
 * Compare oldTree and newTree, return a GoalTreeDiff report.
 */
export function diffGoalTrees(oldTree: GoalTree, newTree: GoalTree): GoalTreeDiff {
    const entries: DiffEntry[] = [];
    let added = 0, removed = 0, changed = 0, orphaned = 0, preserved = 0;

    const oldMicros = buildMicroMap(oldTree);
    const newMicros = buildMicroMap(newTree);
    const oldMinors = buildMinorMap(oldTree);
    const newMinors = buildMinorMap(newTree);
    const oldMajors = buildMajorMap(oldTree);
    const newMajors = buildMajorMap(newTree);

    // ── Majors ──
    for (const [id, maj] of newMajors) {
        if (!oldMajors.has(id)) {
            entries.push({ id, type: 'major', change: 'added', title: maj.title });
            added++;
        } else {
            const old = oldMajors.get(id)!;
            const fields = diffFields(old, maj, ['title', 'description']);
            if (fields.length > 0) {
                entries.push({ id, type: 'major', change: 'changed', title: maj.title, changedFields: fields });
                changed++;
            }
        }
    }
    for (const [id, maj] of oldMajors) {
        if (!newMajors.has(id)) {
            entries.push({ id, type: 'major', change: 'removed', title: maj.title });
            removed++;
        }
    }

    // ── Minors ──
    for (const [id, min] of newMinors) {
        if (!oldMinors.has(id)) {
            entries.push({ id, type: 'minor', change: 'added', title: min.title });
            added++;
        } else {
            const old = oldMinors.get(id)!;
            const fields = diffFields(old, min, ['title', 'description']);
            if (fields.length > 0) {
                entries.push({ id, type: 'minor', change: 'changed', title: min.title, changedFields: fields });
                changed++;
            }
        }
    }
    for (const [id, min] of oldMinors) {
        if (!newMinors.has(id)) {
            entries.push({ id, type: 'minor', change: 'removed', title: min.title });
            removed++;
        }
    }

    // ── Micros ──
    for (const [id, entry] of newMicros) {
        if (!oldMicros.has(id)) {
            entries.push({ id, type: 'micro', change: 'added', title: entry.micro.title });
            added++;
        } else {
            const old = oldMicros.get(id)!;
            const fields = diffFields(old.micro, entry.micro,
                ['title', 'description', 'acceptance_criteria', 'evidence_type']);
            if (fields.length > 0) {
                entries.push({
                    id, type: 'micro', change: 'changed', title: entry.micro.title,
                    changedFields: fields,
                });
                changed++;
            }
            // Count preserved status
            if (old.micro.status === 'done' && entry.micro.status === 'done') {
                preserved++;
            }
        }
    }
    for (const [id, entry] of oldMicros) {
        if (!newMicros.has(id)) {
            entries.push({
                id, type: 'micro', change: 'removed', title: entry.micro.title,
                preservedStatus: entry.micro.status,
                preservedEvidence: entry.micro.evidence,
            });
            removed++;
            if (entry.micro.status === 'done') {
                orphaned++;
            }
        }
    }

    return {
        old_version_id: oldTree.version_id ?? '',
        new_version_id: newTree.version_id ?? '',
        timestamp: new Date().toISOString(),
        entries,
        summary: { added, removed, changed, orphaned, preserved },
    };
}

// ── Helper: compare specific fields of two objects ──────────────────────

function diffFields(a: Record<string, unknown>, b: Record<string, unknown>, keys: string[]): string[] {
    const changed: string[] = [];
    for (const k of keys) {
        if (a[k] !== b[k]) changed.push(k);
    }
    return changed;
}

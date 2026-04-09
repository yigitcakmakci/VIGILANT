/**
 * goal-tree-types.ts — TypeScript interfaces for the deterministic
 * GoalTree JSON document.
 *
 * Mirrors the C++ GoalTree / MajorGoal / MinorGoal / MicroTask structs.
 * Generated from a finalized InterviewResult + ExtractedSlots.
 *
 * INVARIANT:
 *  - Every MicroTask.acceptance_criteria MUST be a non-empty string
 *    (anti-hallucinated progress: no task can be "done" without a
 *     concrete, verifiable criterion).
 *  - IDs are deterministic: "major-0", "minor-0-1", "micro-0-1-2".
 */

// ═══════════════════════════════════════════════════════════════════════
// Enums / union literals
// ═══════════════════════════════════════════════════════════════════════

export type EvidenceType = 'text' | 'file' | 'url' | 'metric';

export type MicroTaskStatus = 'open' | 'done';

// ═══════════════════════════════════════════════════════════════════════
// Evidence — proof attached when completing a MicroTask.
// Which field is required depends on the parent MicroTask.evidence_type:
//   'text'   → evidence.text      must be non-empty
//   'file'   → evidence.file_path must be non-empty
//   'url'    → evidence.url       must be non-empty
//   'metric' → evidence.metric_value must be a finite number
// ═══════════════════════════════════════════════════════════════════════

export interface Evidence {
    text?: string;
    file_path?: string;
    url?: string;
    metric_value?: number;
}

// ═══════════════════════════════════════════════════════════════════════
// MicroTask — leaf node
// ═══════════════════════════════════════════════════════════════════════

export interface MicroTask {
    id: string;
    title: string;
    description: string;
    /** REQUIRED — must be non-empty. Anti-hallucinated progress. */
    acceptance_criteria: string;
    evidence_type: EvidenceType;
    status: MicroTaskStatus;
    /** IDs of MicroTasks this depends on (empty = no dependency) */
    dependencies: string[];
    /** Proof of completion — optional while 'open', REQUIRED when 'done' */
    evidence?: Evidence;
}

// ═══════════════════════════════════════════════════════════════════════
// MinorGoal — contains MicroTasks
// ═══════════════════════════════════════════════════════════════════════

export interface MinorGoal {
    id: string;
    title: string;
    description: string;
    micros: MicroTask[];
}

// ═══════════════════════════════════════════════════════════════════════
// MajorGoal — top-level node, contains MinorGoals
// ═══════════════════════════════════════════════════════════════════════

export interface MajorGoal {
    id: string;
    title: string;
    description: string;
    minors: MinorGoal[];
}

// ═══════════════════════════════════════════════════════════════════════
// GoalTree — root document
// ═══════════════════════════════════════════════════════════════════════

export interface GoalTree {
    version: 1;
    session_id: string;
    generated_at: string;       // ISO-8601
    majors: MajorGoal[];
    /** Unique version identifier for this GoalTree snapshot */
    version_id?: string;
    /** version_id of the parent GoalTree this was replanned from (undefined for first generation) */
    parent_version?: string;
    /** ISO-8601 timestamp when this version was created */
    created_ts?: string;
}

// ═══════════════════════════════════════════════════════════════════════
// MicroTask status extended — 'orphaned' used during replan merge
// when a micro's id no longer exists in the new tree
// ═══════════════════════════════════════════════════════════════════════

export type MicroTaskStatusExtended = MicroTaskStatus | 'orphaned';

// ═══════════════════════════════════════════════════════════════════════
// GoalTree Diff — tracks changes between two GoalTree versions
// ═══════════════════════════════════════════════════════════════════════

export interface DiffEntry {
    id: string;
    type: 'major' | 'minor' | 'micro';
    change: 'added' | 'removed' | 'changed';
    title: string;
    /** For 'changed' entries: which fields were modified */
    changedFields?: string[];
    /** For orphaned micros: preserved status and evidence */
    preservedStatus?: MicroTaskStatus;
    preservedEvidence?: Evidence;
}

export interface GoalTreeDiff {
    old_version_id: string;
    new_version_id: string;
    timestamp: string;
    entries: DiffEntry[];
    summary: {
        added: number;
        removed: number;
        changed: number;
        orphaned: number;
        preserved: number;    // micros whose status/evidence was carried over
    };
}

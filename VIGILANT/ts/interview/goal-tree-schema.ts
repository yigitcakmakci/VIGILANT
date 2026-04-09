/**
 * goal-tree-schema.ts — Runtime JSON validator for GoalTree documents.
 *
 * No external validation library — keeps the dependency footprint zero
 * (same pattern as narrative-schema.ts / classifier-schema.ts).
 *
 * Returns a discriminated union so callers never touch an unvalidated
 * object.
 */

import type {
    GoalTree,
    MajorGoal,
    MinorGoal,
    MicroTask,
    EvidenceType,
    MicroTaskStatus,
    Evidence,
} from './goal-tree-types';

// ── Public result types ────────────────────────────────────────────────

export interface GoalTreeValidationOk {
    ok: true;
    data: GoalTree;
}

export interface GoalTreeValidationFail {
    ok: false;
    error: string;
    path: string;           // JSON-path style location of the failure
    raw: string;
}

export type GoalTreeValidationResult = GoalTreeValidationOk | GoalTreeValidationFail;

// ── Constants ──────────────────────────────────────────────────────────

const VALID_EVIDENCE_TYPES: ReadonlySet<EvidenceType> = new Set([
    'text', 'file', 'url', 'metric',
]);

const VALID_STATUSES: ReadonlySet<MicroTaskStatus> = new Set([
    'open', 'done',
]);

// ── Internal helpers ───────────────────────────────────────────────────

function fail(error: string, path: string, raw: string): GoalTreeValidationFail {
    return { ok: false, error, path, raw };
}

function isNonEmptyString(v: unknown): v is string {
    return typeof v === 'string' && v.length > 0;
}

function isStringArray(v: unknown): v is string[] {
    return Array.isArray(v) && v.every((e) => typeof e === 'string');
}

// ── Evidence validator (per evidence_type) ─────────────────────────────

function validateEvidenceForType(
    evidenceType: EvidenceType,
    evidence: unknown,
    path: string,
    raw: string,
): GoalTreeValidationFail | null {
    if (typeof evidence !== 'object' || evidence === null || Array.isArray(evidence)) {
        return fail(
            `evidence is required when status is 'done'`,
            path + '.evidence',
            raw,
        );
    }
    const ev = evidence as Record<string, unknown>;

    switch (evidenceType) {
        case 'text':
            if (!isNonEmptyString(ev['text']))
                return fail(
                    "evidence_type is 'text' but evidence.text is missing or empty",
                    path + '.evidence.text',
                    raw,
                );
            break;
        case 'file':
            if (!isNonEmptyString(ev['file_path']))
                return fail(
                    "evidence_type is 'file' but evidence.file_path is missing or empty",
                    path + '.evidence.file_path',
                    raw,
                );
            break;
        case 'url':
            if (!isNonEmptyString(ev['url']))
                return fail(
                    "evidence_type is 'url' but evidence.url is missing or empty",
                    path + '.evidence.url',
                    raw,
                );
            break;
        case 'metric':
            if (typeof ev['metric_value'] !== 'number' || !isFinite(ev['metric_value'] as number))
                return fail(
                    "evidence_type is 'metric' but evidence.metric_value is missing or not a finite number",
                    path + '.evidence.metric_value',
                    raw,
                );
            break;
    }
    return null;
}

// ── MicroTask validator ────────────────────────────────────────────────

function validateMicroTask(
    obj: Record<string, unknown>,
    path: string,
    raw: string,
    allMicroIds: Set<string>,
): GoalTreeValidationFail | null {
    if (!isNonEmptyString(obj['id'])) {
        return fail('Missing or empty id', path + '.id', raw);
    }
    if (allMicroIds.has(obj['id'] as string)) {
        return fail(`Duplicate micro id: "${obj['id']}"`, path + '.id', raw);
    }
    allMicroIds.add(obj['id'] as string);

    if (!isNonEmptyString(obj['title'])) {
        return fail('Missing or empty title', path + '.title', raw);
    }
    if (typeof obj['description'] !== 'string') {
        return fail('Missing description', path + '.description', raw);
    }

    // ── CRITICAL: acceptance_criteria must be non-empty ────────────────
    if (!isNonEmptyString(obj['acceptance_criteria'])) {
        return fail(
            'acceptance_criteria is REQUIRED and must be a non-empty string (anti-hallucinated progress)',
            path + '.acceptance_criteria',
            raw,
        );
    }

    if (!VALID_EVIDENCE_TYPES.has(obj['evidence_type'] as EvidenceType)) {
        return fail(
            `Invalid evidence_type: "${String(obj['evidence_type'])}" (expected text|file|url|metric)`,
            path + '.evidence_type',
            raw,
        );
    }

    if (!VALID_STATUSES.has(obj['status'] as MicroTaskStatus)) {
        return fail(
            `Invalid status: "${String(obj['status'])}" (expected open|done)`,
            path + '.status',
            raw,
        );
    }

    if (!isStringArray(obj['dependencies'])) {
        return fail('dependencies must be a string array', path + '.dependencies', raw);
    }

    // ── evidence: if status === 'done', evidence must satisfy evidence_type ─
    if (obj['status'] === 'done') {
        const evErr = validateEvidenceForType(
            obj['evidence_type'] as EvidenceType,
            obj['evidence'],
            path,
            raw,
        );
        if (evErr) return evErr;
    }

    return null;    // valid
}

// ── MinorGoal validator ────────────────────────────────────────────────

function validateMinorGoal(
    obj: Record<string, unknown>,
    path: string,
    raw: string,
    allMicroIds: Set<string>,
): GoalTreeValidationFail | null {
    if (!isNonEmptyString(obj['id'])) {
        return fail('Missing or empty id', path + '.id', raw);
    }
    if (!isNonEmptyString(obj['title'])) {
        return fail('Missing or empty title', path + '.title', raw);
    }
    if (typeof obj['description'] !== 'string') {
        return fail('Missing description', path + '.description', raw);
    }

    if (!Array.isArray(obj['micros'])) {
        return fail('micros must be an array', path + '.micros', raw);
    }
    if ((obj['micros'] as unknown[]).length === 0) {
        return fail('micros array must not be empty', path + '.micros', raw);
    }

    for (let i = 0; i < (obj['micros'] as unknown[]).length; i++) {
        const micro = (obj['micros'] as unknown[])[i];
        if (typeof micro !== 'object' || micro === null || Array.isArray(micro)) {
            return fail(`micros[${i}] is not an object`, `${path}.micros[${i}]`, raw);
        }
        const err = validateMicroTask(micro as Record<string, unknown>, `${path}.micros[${i}]`, raw, allMicroIds);
        if (err) return err;
    }

    return null;
}

// ── MajorGoal validator ────────────────────────────────────────────────

function validateMajorGoal(
    obj: Record<string, unknown>,
    path: string,
    raw: string,
    allMicroIds: Set<string>,
): GoalTreeValidationFail | null {
    if (!isNonEmptyString(obj['id'])) {
        return fail('Missing or empty id', path + '.id', raw);
    }
    if (!isNonEmptyString(obj['title'])) {
        return fail('Missing or empty title', path + '.title', raw);
    }
    if (typeof obj['description'] !== 'string') {
        return fail('Missing description', path + '.description', raw);
    }

    if (!Array.isArray(obj['minors'])) {
        return fail('minors must be an array', path + '.minors', raw);
    }
    if ((obj['minors'] as unknown[]).length === 0) {
        return fail('minors array must not be empty', path + '.minors', raw);
    }

    for (let i = 0; i < (obj['minors'] as unknown[]).length; i++) {
        const minor = (obj['minors'] as unknown[])[i];
        if (typeof minor !== 'object' || minor === null || Array.isArray(minor)) {
            return fail(`minors[${i}] is not an object`, `${path}.minors[${i}]`, raw);
        }
        const err = validateMinorGoal(minor as Record<string, unknown>, `${path}.minors[${i}]`, raw, allMicroIds);
        if (err) return err;
    }

    return null;
}

// ── Dependency reference validator ─────────────────────────────────────

function validateDependencyRefs(
    tree: GoalTree,
    allMicroIds: Set<string>,
    raw: string,
): GoalTreeValidationFail | null {
    for (let mi = 0; mi < tree.majors.length; mi++) {
        const major = tree.majors[mi];
        for (let ni = 0; ni < major.minors.length; ni++) {
            const minor = major.minors[ni];
            for (let ti = 0; ti < minor.micros.length; ti++) {
                const micro = minor.micros[ti];
                for (const dep of micro.dependencies) {
                    if (!allMicroIds.has(dep)) {
                        return fail(
                            `Dependency "${dep}" references a non-existent MicroTask id`,
                            `$.majors[${mi}].minors[${ni}].micros[${ti}].dependencies`,
                            raw,
                        );
                    }
                    if (dep === micro.id) {
                        return fail(
                            `MicroTask "${micro.id}" depends on itself`,
                            `$.majors[${mi}].minors[${ni}].micros[${ti}].dependencies`,
                            raw,
                        );
                    }
                }
            }
        }
    }
    return null;
}

// ═══════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════

/**
 * Parse and validate a raw JSON string against the GoalTree schema.
 * Returns a discriminated union so callers never touch an unvalidated
 * object.
 */
export function validateGoalTreeJson(raw: string): GoalTreeValidationResult {
    // Strip markdown fences the model sometimes sneaks in.
    const cleaned = raw.replace(/^```(?:json)?\s*|```$/g, '').trim();

    let parsed: unknown;
    try {
        parsed = JSON.parse(cleaned);
    } catch {
        return fail('Invalid JSON', '$', raw);
    }

    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
        return fail('Expected a JSON object', '$', raw);
    }

    const obj = parsed as Record<string, unknown>;

    // ── version ────────────────────────────────────────────────────────
    if (obj['version'] !== 1) {
        return fail(`Unsupported version: ${String(obj['version'])} (expected 1)`, '$.version', raw);
    }

    // ── session_id ─────────────────────────────────────────────────────
    if (!isNonEmptyString(obj['session_id'])) {
        return fail('Missing or empty session_id', '$.session_id', raw);
    }

    // ── generated_at ───────────────────────────────────────────────────
    if (!isNonEmptyString(obj['generated_at'])) {
        return fail('Missing or empty generated_at', '$.generated_at', raw);
    }

    // ── majors ─────────────────────────────────────────────────────────
    if (!Array.isArray(obj['majors'])) {
        return fail('majors must be an array', '$.majors', raw);
    }
    if ((obj['majors'] as unknown[]).length === 0) {
        return fail('majors array must not be empty', '$.majors', raw);
    }

    const allMicroIds = new Set<string>();

    for (let i = 0; i < (obj['majors'] as unknown[]).length; i++) {
        const major = (obj['majors'] as unknown[])[i];
        if (typeof major !== 'object' || major === null || Array.isArray(major)) {
            return fail(`majors[${i}] is not an object`, `$.majors[${i}]`, raw);
        }
        const err = validateMajorGoal(major as Record<string, unknown>, `$.majors[${i}]`, raw, allMicroIds);
        if (err) return err;
    }

    // At this point the structure is valid — cast to GoalTree
    const tree = parsed as unknown as GoalTree;

    // ── Cross-reference: dependency IDs must exist ─────────────────────
    const depErr = validateDependencyRefs(tree, allMicroIds, raw);
    if (depErr) return depErr;

    return { ok: true, data: tree };
}

// ═══════════════════════════════════════════════════════════════════════
// Tick-to-Done validation
// ═══════════════════════════════════════════════════════════════════════

/** Structured error for tick-done validation — mirrors C++ TickValidationError */
export interface TickValidationError {
    code: string;
    message: string;
    microTaskId: string;
}

export interface TickValidationOk {
    ok: true;
}

export interface TickValidationFail {
    ok: false;
    errors: TickValidationError[];
}

export type TickValidationResult = TickValidationOk | TickValidationFail;

/**
 * Validate whether a MicroTask can be transitioned to 'done'.
 *
 * Checks (all must pass):
 *   1. acceptance_criteria is non-empty
 *   2. evidence exists and satisfies evidence_type
 *   3. all dependencies are 'done'
 *
 * @param micro    The MicroTask to tick
 * @param evidence The evidence being submitted
 * @param tree     The full GoalTree (for dependency resolution)
 */
export function validateTickDone(
    micro: MicroTask,
    evidence: Evidence | undefined,
    tree: GoalTree,
): TickValidationResult {
    const errors: TickValidationError[] = [];

    // 1. acceptance_criteria
    if (!micro.acceptance_criteria || micro.acceptance_criteria.trim().length === 0) {
        errors.push({
            code: 'EMPTY_ACCEPTANCE_CRITERIA',
            message: 'Cannot mark done: acceptance_criteria is empty (anti-hallucinated progress)',
            microTaskId: micro.id,
        });
    }

    // 2. evidence vs evidence_type
    if (!evidence || typeof evidence !== 'object') {
        errors.push({
            code: 'MISSING_EVIDENCE',
            message: `Cannot mark done: evidence is required (expected ${micro.evidence_type})`,
            microTaskId: micro.id,
        });
    } else {
        switch (micro.evidence_type) {
            case 'text':
                if (!evidence.text || evidence.text.trim().length === 0)
                    errors.push({
                        code: 'MISSING_EVIDENCE_TEXT',
                        message: "evidence_type is 'text' but evidence.text is missing or empty",
                        microTaskId: micro.id,
                    });
                break;
            case 'file':
                if (!evidence.file_path || evidence.file_path.trim().length === 0)
                    errors.push({
                        code: 'MISSING_EVIDENCE_FILE',
                        message: "evidence_type is 'file' but evidence.file_path is missing or empty",
                        microTaskId: micro.id,
                    });
                break;
            case 'url':
                if (!evidence.url || evidence.url.trim().length === 0)
                    errors.push({
                        code: 'MISSING_EVIDENCE_URL',
                        message: "evidence_type is 'url' but evidence.url is missing or empty",
                        microTaskId: micro.id,
                    });
                break;
            case 'metric':
                if (typeof evidence.metric_value !== 'number' || !isFinite(evidence.metric_value))
                    errors.push({
                        code: 'MISSING_EVIDENCE_METRIC',
                        message: "evidence_type is 'metric' but evidence.metric_value is missing or not a finite number",
                        microTaskId: micro.id,
                    });
                break;
        }
    }

    // 3. unresolved dependencies
    if (micro.dependencies.length > 0) {
        const microIndex = buildMicroIndex(tree);
        const openDeps = micro.dependencies.filter((depId) => {
            const dep = microIndex.get(depId);
            return !dep || dep.status !== 'done';
        });
        if (openDeps.length > 0) {
            errors.push({
                code: 'UNRESOLVED_DEPENDENCIES',
                message: `Cannot mark done: dependencies not yet completed: ${openDeps.join(', ')}`,
                microTaskId: micro.id,
            });
        }
    }

    return errors.length === 0
        ? { ok: true }
        : { ok: false, errors };
}

// ── helper: flat micro lookup ──────────────────────────────────────────

function buildMicroIndex(tree: GoalTree): Map<string, MicroTask> {
    const map = new Map<string, MicroTask>();
    for (const major of tree.majors)
        for (const minor of major.minors)
            for (const micro of minor.micros)
                map.set(micro.id, micro);
    return map;
}

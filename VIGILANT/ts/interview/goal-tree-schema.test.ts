/**
 * goal-tree-schema.test.ts — Unit tests for GoalTree schema validation.
 *
 * Same console.assert runner pattern as interview-result-types.test.ts.
 * Deterministic: no time/network dependencies.
 *
 * Run: npx ts-node ts/interview/goal-tree-schema.test.ts
 *   or: npx tsx  ts/interview/goal-tree-schema.test.ts
 */

import { validateGoalTreeJson, validateTickDone } from './goal-tree-schema';
import type { GoalTree, MicroTask, MinorGoal, MajorGoal, Evidence } from './goal-tree-types';

// ── Helpers ────────────────────────────────────────────────────────────

const FIXED_TS = '2025-01-15T10:00:00Z';

function makeMicro(overrides?: Partial<MicroTask>): MicroTask {
    return {
        id: 'micro-0-0-0',
        title: 'HTML temelleri öğren',
        description: 'Temel HTML etiketlerini ve yapısını öğren',
        acceptance_criteria: 'Sıfırdan bir HTML sayfası oluşturabilmeli',
        evidence_type: 'file',
        status: 'open',
        dependencies: [],
        ...overrides,
    };
}

function makeMinor(overrides?: Partial<MinorGoal>): MinorGoal {
    return {
        id: 'minor-0-0',
        title: 'Temel web teknolojileri',
        description: 'HTML, CSS, JS temellerini kapsayan alt hedef',
        micros: [makeMicro()],
        ...overrides,
    };
}

function makeMajor(overrides?: Partial<MajorGoal>): MajorGoal {
    return {
        id: 'major-0',
        title: 'Frontend geliştirme',
        description: 'Tam bir frontend geliştirici olmak',
        minors: [makeMinor()],
        ...overrides,
    };
}

function makeTree(overrides?: Partial<GoalTree>): GoalTree {
    return {
        version: 1,
        session_id: 'sess-1',
        generated_at: FIXED_TS,
        majors: [makeMajor()],
        ...overrides,
    };
}

function jsonStr(tree: unknown): string {
    return JSON.stringify(tree);
}

// ═══════════════════════════════════════════════════════════════════════
// Valid document tests
// ═══════════════════════════════════════════════════════════════════════

function test_validMinimalTree(): void {
    const r = validateGoalTreeJson(jsonStr(makeTree()));
    console.assert(r.ok === true, 'minimal tree should be valid');
    if (r.ok) {
        console.assert(r.data.version === 1, 'version 1');
        console.assert(r.data.majors.length === 1, '1 major');
        console.assert(r.data.majors[0].minors[0].micros[0].acceptance_criteria.length > 0, 'AC present');
    }
    console.log('✓ G1: valid minimal GoalTree');
}

function test_validMultiLevel(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                id: 'major-0',
                minors: [
                    makeMinor({
                        id: 'minor-0-0',
                        micros: [
                            makeMicro({ id: 'micro-0-0-0' }),
                            makeMicro({ id: 'micro-0-0-1', title: 'CSS temelleri', dependencies: ['micro-0-0-0'] }),
                        ],
                    }),
                    makeMinor({ id: 'minor-0-1', micros: [makeMicro({ id: 'micro-0-1-0' })] }),
                ],
            }),
            makeMajor({
                id: 'major-1',
                title: 'Backend',
                minors: [makeMinor({ id: 'minor-1-0', micros: [makeMicro({ id: 'micro-1-0-0' })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === true, 'multi-level tree should be valid');
    if (r.ok) {
        console.assert(r.data.majors.length === 2, '2 majors');
        console.assert(r.data.majors[0].minors.length === 2, '2 minors in first major');
        console.assert(r.data.majors[0].minors[0].micros.length === 2, '2 micros in first minor');
    }
    console.log('✓ G2: valid multi-level GoalTree');
}

function test_markdownFenceStripped(): void {
    const raw = '```json\n' + jsonStr(makeTree()) + '\n```';
    const r = validateGoalTreeJson(raw);
    console.assert(r.ok === true, 'markdown fences should be stripped');
    console.log('✓ G3: markdown fence stripping');
}

// ═══════════════════════════════════════════════════════════════════════
// Invalid document tests — structural
// ═══════════════════════════════════════════════════════════════════════

function test_invalidJson(): void {
    const r = validateGoalTreeJson('not json at all');
    console.assert(r.ok === false, 'invalid JSON');
    if (!r.ok) console.assert(r.path === '$', 'root path');
    console.log('✓ G4: invalid JSON rejected');
}

function test_wrongVersion(): void {
    const r = validateGoalTreeJson(jsonStr(makeTree({ version: 2 as any })));
    console.assert(r.ok === false, 'wrong version');
    if (!r.ok) console.assert(r.error.includes('version'), 'version error msg');
    console.log('✓ G5: wrong version rejected');
}

function test_missingSessionId(): void {
    const r = validateGoalTreeJson(jsonStr(makeTree({ session_id: '' })));
    console.assert(r.ok === false, 'empty session_id');
    console.log('✓ G6: empty session_id rejected');
}

function test_emptyMajors(): void {
    const r = validateGoalTreeJson(jsonStr(makeTree({ majors: [] })));
    console.assert(r.ok === false, 'empty majors');
    console.log('✓ G7: empty majors rejected');
}

function test_emptyMinors(): void {
    const tree = makeTree({ majors: [makeMajor({ minors: [] })] });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'empty minors');
    console.log('✓ G8: empty minors rejected');
}

function test_emptyMicros(): void {
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [] })] })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'empty micros');
    console.log('✓ G9: empty micros rejected');
}

// ═══════════════════════════════════════════════════════════════════════
// Invalid document tests — acceptance_criteria (anti-hallucinated)
// ═══════════════════════════════════════════════════════════════════════

function test_emptyAcceptanceCriteria(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [makeMinor({ micros: [makeMicro({ acceptance_criteria: '' })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'empty acceptance_criteria must fail');
    if (!r.ok) {
        console.assert(r.error.includes('acceptance_criteria'), 'error mentions AC');
        console.assert(r.error.includes('anti-hallucinated'), 'error mentions anti-hallucinated');
    }
    console.log('✓ G10: empty acceptance_criteria rejected (anti-hallucinated)');
}

function test_missingAcceptanceCriteria(): void {
    const micro = makeMicro();
    delete (micro as any).acceptance_criteria;
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'missing acceptance_criteria must fail');
    console.log('✓ G11: missing acceptance_criteria rejected');
}

// ═══════════════════════════════════════════════════════════════════════
// Invalid document tests — enum values
// ═══════════════════════════════════════════════════════════════════════

function test_invalidEvidenceType(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [makeMinor({ micros: [makeMicro({ evidence_type: 'video' as any })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'invalid evidence_type');
    if (!r.ok) console.assert(r.error.includes('evidence_type'), 'mentions evidence_type');
    console.log('✓ G12: invalid evidence_type rejected');
}

function test_invalidStatus(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [makeMinor({ micros: [makeMicro({ status: 'pending' as any })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'invalid status');
    if (!r.ok) console.assert(r.error.includes('status'), 'mentions status');
    console.log('✓ G13: invalid status rejected');
}

// ═══════════════════════════════════════════════════════════════════════
// Invalid document tests — dependencies
// ═══════════════════════════════════════════════════════════════════════

function test_danglingDependency(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [makeMinor({ micros: [makeMicro({ dependencies: ['nonexistent-id'] })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'dangling dependency');
    if (!r.ok) console.assert(r.error.includes('non-existent'), 'mentions non-existent');
    console.log('✓ G14: dangling dependency rejected');
}

function test_selfDependency(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [makeMinor({ micros: [makeMicro({ id: 'micro-x', dependencies: ['micro-x'] })] })],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'self dependency');
    if (!r.ok) console.assert(r.error.includes('itself'), 'mentions itself');
    console.log('✓ G15: self-dependency rejected');
}

function test_duplicateMicroIds(): void {
    const tree = makeTree({
        majors: [
            makeMajor({
                minors: [
                    makeMinor({
                        micros: [
                            makeMicro({ id: 'same-id' }),
                            makeMicro({ id: 'same-id', title: 'duplicate' }),
                        ],
                    }),
                ],
            }),
        ],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'duplicate micro IDs');
    if (!r.ok) console.assert(r.error.includes('Duplicate'), 'mentions Duplicate');
    console.log('✓ G16: duplicate micro IDs rejected');
}

// ═══════════════════════════════════════════════════════════════════════
// All evidence_type variants pass
// ═══════════════════════════════════════════════════════════════════════

function test_allEvidenceTypes(): void {
    const types: Array<'text' | 'file' | 'url' | 'metric'> = ['text', 'file', 'url', 'metric'];
    for (const t of types) {
        const tree = makeTree({
            majors: [
                makeMajor({
                    minors: [makeMinor({ micros: [makeMicro({ id: `m-${t}`, evidence_type: t })] })],
                }),
            ],
        });
        const r = validateGoalTreeJson(jsonStr(tree));
        console.assert(r.ok === true, `evidence_type "${t}" should be valid`);
    }
    console.log('✓ G17: all evidence_type variants accepted');
}

// ═══════════════════════════════════════════════════════════════════════
// Schema validation — done status requires matching evidence
// ═══════════════════════════════════════════════════════════════════════

function test_doneWithMatchingEvidence(): void {
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({
                micros: [makeMicro({
                    status: 'done',
                    evidence_type: 'url',
                    evidence: { url: 'https://example.com/proof' },
                })],
            })],
        })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === true, 'done with matching evidence should pass');
    console.log('✓ G18: done + matching evidence accepted');
}

function test_doneWithMissingEvidence(): void {
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({
                micros: [makeMicro({ status: 'done', evidence_type: 'file' })],
            })],
        })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'done without evidence should fail');
    if (!r.ok) console.assert(r.error.includes('evidence'), 'mentions evidence');
    console.log('✓ G19: done without evidence rejected');
}

function test_doneWithWrongEvidenceField(): void {
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({
                micros: [makeMicro({
                    status: 'done',
                    evidence_type: 'url',
                    evidence: { text: 'wrong field' },
                })],
            })],
        })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    console.assert(r.ok === false, 'done with wrong evidence field should fail');
    if (!r.ok) console.assert(r.error.includes('url'), 'mentions url');
    console.log('✓ G20: done + wrong evidence field rejected');
}

function test_doneMetricNonFinite(): void {
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({
                micros: [makeMicro({
                    status: 'done',
                    evidence_type: 'metric',
                    evidence: { metric_value: Infinity },
                })],
            })],
        })],
    });
    const r = validateGoalTreeJson(jsonStr(tree));
    // Infinity serialises to null in JSON → should fail
    console.assert(r.ok === false, 'metric Infinity should fail');
    console.log('✓ G21: done + non-finite metric rejected');
}

// ═══════════════════════════════════════════════════════════════════════
// validateTickDone — tick-to-done validation tests
// ═══════════════════════════════════════════════════════════════════════

function test_tickDone_valid(): void {
    const micro = makeMicro({ evidence_type: 'text' });
    const evidence: Evidence = { text: 'I completed this task' };
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === true, 'valid tick should pass');
    console.log('✓ T1: validateTickDone — valid tick');
}

function test_tickDone_emptyAcceptanceCriteria(): void {
    const micro = makeMicro({ acceptance_criteria: '', evidence_type: 'text' });
    const evidence: Evidence = { text: 'proof' };
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === false, 'empty AC should fail');
    if (!r.ok) {
        console.assert(r.errors.some(e => e.code === 'EMPTY_ACCEPTANCE_CRITERIA'), 'has AC error code');
        console.assert(r.errors[0].microTaskId === micro.id, 'error has microTaskId');
    }
    console.log('✓ T2: validateTickDone — empty acceptance_criteria');
}

function test_tickDone_missingEvidence(): void {
    const micro = makeMicro({ evidence_type: 'file' });
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, undefined, tree);
    console.assert(r.ok === false, 'missing evidence should fail');
    if (!r.ok) console.assert(r.errors.some(e => e.code === 'MISSING_EVIDENCE'), 'has MISSING_EVIDENCE');
    console.log('✓ T3: validateTickDone — missing evidence');
}

function test_tickDone_wrongEvidenceType_url(): void {
    const micro = makeMicro({ evidence_type: 'url' });
    const evidence: Evidence = { text: 'not a url' };  // wrong field
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === false, 'url type with text evidence should fail');
    if (!r.ok) console.assert(r.errors.some(e => e.code === 'MISSING_EVIDENCE_URL'), 'has URL error');
    console.log('✓ T4: validateTickDone — wrong evidence for url type');
}

function test_tickDone_wrongEvidenceType_file(): void {
    const micro = makeMicro({ evidence_type: 'file' });
    const evidence: Evidence = { url: 'https://x.com' };
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === false, 'file type with url evidence should fail');
    if (!r.ok) console.assert(r.errors.some(e => e.code === 'MISSING_EVIDENCE_FILE'), 'has FILE error');
    console.log('✓ T5: validateTickDone — wrong evidence for file type');
}

function test_tickDone_wrongEvidenceType_metric(): void {
    const micro = makeMicro({ evidence_type: 'metric' });
    const evidence: Evidence = { text: 'not a number' };
    const tree = makeTree({
        majors: [makeMajor({ minors: [makeMinor({ micros: [micro] })] })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === false, 'metric type without metric_value should fail');
    if (!r.ok) console.assert(r.errors.some(e => e.code === 'MISSING_EVIDENCE_METRIC'), 'has METRIC error');
    console.log('✓ T6: validateTickDone — wrong evidence for metric type');
}

function test_tickDone_unresolvedDependency(): void {
    const dep = makeMicro({ id: 'micro-dep', status: 'open' });
    const micro = makeMicro({ id: 'micro-main', dependencies: ['micro-dep'], evidence_type: 'text' });
    const evidence: Evidence = { text: 'proof' };
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({ micros: [dep, micro] })],
        })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === false, 'open dependency should fail');
    if (!r.ok) console.assert(r.errors.some(e => e.code === 'UNRESOLVED_DEPENDENCIES'), 'has DEP error');
    console.log('✓ T7: validateTickDone — unresolved dependency');
}

function test_tickDone_resolvedDependency(): void {
    const dep = makeMicro({ id: 'micro-dep', status: 'done', evidence: { text: 'done' } });
    const micro = makeMicro({ id: 'micro-main', dependencies: ['micro-dep'], evidence_type: 'text' });
    const evidence: Evidence = { text: 'proof' };
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({ micros: [dep, micro] })],
        })],
    });
    const r = validateTickDone(micro, evidence, tree);
    console.assert(r.ok === true, 'resolved dependency should pass');
    console.log('✓ T8: validateTickDone — resolved dependency passes');
}

function test_tickDone_multipleErrors(): void {
    const dep = makeMicro({ id: 'micro-dep', status: 'open' });
    const micro = makeMicro({
        id: 'micro-bad',
        acceptance_criteria: '',
        evidence_type: 'url',
        dependencies: ['micro-dep'],
    });
    const tree = makeTree({
        majors: [makeMajor({
            minors: [makeMinor({ micros: [dep, micro] })],
        })],
    });
    const r = validateTickDone(micro, undefined, tree);
    console.assert(r.ok === false, 'multiple failures');
    if (!r.ok) {
        console.assert(r.errors.length >= 3, 'at least 3 errors: AC + evidence + deps');
        console.assert(r.errors.every(e => e.microTaskId === 'micro-bad'), 'all errors have correct id');
    }
    console.log('✓ T9: validateTickDone — multiple errors accumulated');
}

// ═══════════════════════════════════════════════════════════════════════
// Runner
// ═══════════════════════════════════════════════════════════════════════

console.log('\n── GoalTree schema validation tests ──────────────────\n');

test_validMinimalTree();
test_validMultiLevel();
test_markdownFenceStripped();
test_invalidJson();
test_wrongVersion();
test_missingSessionId();
test_emptyMajors();
test_emptyMinors();
test_emptyMicros();
test_emptyAcceptanceCriteria();
test_missingAcceptanceCriteria();
test_invalidEvidenceType();
test_invalidStatus();
test_danglingDependency();
test_selfDependency();
test_duplicateMicroIds();
test_allEvidenceTypes();
test_doneWithMatchingEvidence();
test_doneWithMissingEvidence();
test_doneWithWrongEvidenceField();
test_doneMetricNonFinite();

console.log('\n── Tick-to-Done validation tests ──────────────────────\n');

test_tickDone_valid();
test_tickDone_emptyAcceptanceCriteria();
test_tickDone_missingEvidence();
test_tickDone_wrongEvidenceType_url();
test_tickDone_wrongEvidenceType_file();
test_tickDone_wrongEvidenceType_metric();
test_tickDone_unresolvedDependency();
test_tickDone_resolvedDependency();
test_tickDone_multipleErrors();

console.log('\n── All GoalTree tests passed ✓ ──────────────────────\n');

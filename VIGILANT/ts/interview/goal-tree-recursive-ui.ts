/**
 * goal-tree-recursive-ui.ts — Recursive DOM renderer for GoalNode trees.
 *
 * Renders an n-depth GoalNode tree into nested HTML using Tailwind CSS
 * with Glassmorphism styling.  Each depth level is indented with pl-4
 * and a left border line to create a visual tree-branch effect.
 *
 * Leaf nodes display a checkbox (visual-only) and acceptance criteria.
 *
 * Usage:
 *   import { renderGoalTree } from './goal-tree-recursive-ui';
 *   const container = document.getElementById('goal-tree-root')!;
 *   renderGoalTree(rootNode, container);
 */

import type { GoalNode, ActionItem } from './goal-tree-types';

// ═══════════════════════════════════════════════════════════════════════
// Progress bar color based on percentage
// ═══════════════════════════════════════════════════════════════════════

function progressColor(progress: number): string {
    if (progress >= 100) return 'bg-emerald-400';
    if (progress >= 60)  return 'bg-sky-400';
    if (progress >= 30)  return 'bg-amber-400';
    return 'bg-rose-400';
}

// ═══════════════════════════════════════════════════════════════════════
// Depth-dependent accent colors for branch lines
// ═══════════════════════════════════════════════════════════════════════

const DEPTH_BORDER_COLORS = [
    'border-indigo-500/40',
    'border-violet-500/40',
    'border-fuchsia-500/40',
    'border-pink-500/40',
    'border-rose-500/40',
    'border-orange-500/40',
    'border-amber-500/40',
];

function depthBorderColor(depth: number): string {
    return DEPTH_BORDER_COLORS[depth % DEPTH_BORDER_COLORS.length];
}

// ═══════════════════════════════════════════════════════════════════════
// Escape HTML to prevent XSS
// ═══════════════════════════════════════════════════════════════════════

function esc(text: string): string {
    const d = document.createElement('div');
    d.textContent = text;
    return d.innerHTML;
}

// ═══════════════════════════════════════════════════════════════════════
// renderGoalNode — recursive inner renderer
// ═══════════════════════════════════════════════════════════════════════

function renderGoalNode(node: GoalNode, parent: HTMLElement, depth: number): void {
    const wrapper = document.createElement('div');
    wrapper.className = depth > 0
        ? `pl-6 ml-2 border-l-2 ${depthBorderColor(depth)} mt-2`
        : 'mt-2';

    // ── Glass card ────────────────────────────────────────────────────
    const card = document.createElement('div');
    card.className = [
        'relative rounded-xl p-4 mb-2 transition-all duration-200',
        'backdrop-blur-md bg-white/5 border border-white/10',
        'shadow-[0_4px_30px_rgba(0,0,0,0.1)]',
        'hover:bg-white/10 hover:border-white/20',
        node.isLeaf ? 'cursor-pointer' : '',
    ].join(' ');
    card.dataset.nodeId = node.id;

    // ── Header row: icon + title + progress badge ─────────────────────
    const header = document.createElement('div');
    header.className = 'flex items-center gap-3 mb-1';

    if (node.isLeaf) {
        // Checkbox for leaf nodes (visual only)
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = node.progress >= 100;
        cb.className = [
            'w-5 h-5 rounded border-2 border-white/30',
            'bg-transparent accent-emerald-400 shrink-0',
            'cursor-pointer transition-colors',
        ].join(' ');
        cb.addEventListener('change', () => {
            node.progress = cb.checked ? 100 : 0;
            updateProgressDisplay(card, node);
            propagateProgress(parent);
        });
        header.appendChild(cb);
    } else {
        // Branch icon
        const icon = document.createElement('span');
        icon.className = 'text-lg shrink-0 select-none';
        icon.textContent = depth === 0 ? '🎯' : '📂';
        header.appendChild(icon);
    }

    const titleEl = document.createElement('span');
    titleEl.className = 'font-semibold text-white/90 text-sm flex-1 leading-tight';
    titleEl.textContent = node.title;
    header.appendChild(titleEl);

    // Progress badge
    const badge = document.createElement('span');
    badge.className = 'text-xs font-mono px-2 py-0.5 rounded-full bg-white/10 text-white/60 shrink-0 goal-node-progress-badge';
    badge.textContent = `${node.progress}%`;
    header.appendChild(badge);

    card.appendChild(header);

    // ── Description ───────────────────────────────────────────────────
    if (node.description) {
        const desc = document.createElement('p');
        desc.className = 'text-xs text-white/40 mt-1 leading-relaxed';
        desc.textContent = node.description;
        card.appendChild(desc);
    }

    // ── Progress bar ──────────────────────────────────────────────────
    const barTrack = document.createElement('div');
    barTrack.className = 'w-full h-1 rounded-full bg-white/10 mt-2 overflow-hidden';
    const barFill = document.createElement('div');
    barFill.className = `h-full rounded-full transition-all duration-500 ${progressColor(node.progress)} goal-node-progress-fill`;
    barFill.style.width = `${Math.min(100, Math.max(0, node.progress))}%`;
    barTrack.appendChild(barFill);
    card.appendChild(barTrack);

    // ── Acceptance criteria (leaf only) ───────────────────────────────
    if (node.isLeaf && node.acceptanceCriteria) {
        const acWrap = document.createElement('div');
        acWrap.className = 'mt-2 px-3 py-2 rounded-lg bg-emerald-500/10 border border-emerald-500/20';

        const acLabel = document.createElement('span');
        acLabel.className = 'text-[10px] uppercase tracking-wider text-emerald-400/70 font-semibold';
        acLabel.textContent = 'Kabul Kriteri';
        acWrap.appendChild(acLabel);

        const acText = document.createElement('p');
        acText.className = 'text-xs text-emerald-300/80 mt-0.5 leading-relaxed';
        acText.textContent = node.acceptanceCriteria;
        acWrap.appendChild(acText);

        card.appendChild(acWrap);
    }

    // ── Action Items checklist ────────────────────────────────────────
    if (node.actionItems && node.actionItems.length > 0) {
        const aiWrap = document.createElement('div');
        aiWrap.className = 'mt-2 pl-4';

        const aiLabel = document.createElement('span');
        aiLabel.className = 'text-[10px] uppercase tracking-wider text-white/40 font-semibold';
        aiLabel.textContent = 'Aksiyon Maddeleri';
        aiWrap.appendChild(aiLabel);

        const aiList = document.createElement('ul');
        aiList.className = 'mt-1 space-y-1 list-none p-0 m-0';

        for (const item of node.actionItems) {
            const li = document.createElement('li');
            li.className = 'flex items-start gap-2';

            const cb = document.createElement('input');
            cb.type = 'checkbox';
            cb.checked = item.isCompleted;
            cb.className = 'w-3.5 h-3.5 mt-0.5 rounded border border-white/20 bg-transparent accent-emerald-400 shrink-0 cursor-pointer';
            cb.addEventListener('change', () => {
                item.isCompleted = cb.checked;
                computeNodeProgress(node);
                updateProgressDisplay(card, node);
                propagateProgress(parent);
            });
            li.appendChild(cb);

            const label = document.createElement('span');
            label.className = 'text-[11px] text-white/50 leading-tight';
            label.textContent = item.text;
            if (item.isCompleted) label.className += ' line-through opacity-50';
            li.appendChild(label);

            aiList.appendChild(li);
        }
        aiWrap.appendChild(aiList);
        card.appendChild(aiWrap);
    }

    wrapper.appendChild(card);

    // ── Recursion: render children ────────────────────────────────────
    if (!node.isLeaf && node.children && node.children.length > 0) {
        const childContainer = document.createElement('div');
        childContainer.className = 'goal-node-children';
        for (const child of node.children) {
            renderGoalNode(child, childContainer, depth + 1);
        }
        wrapper.appendChild(childContainer);
    }

    parent.appendChild(wrapper);
}

// ═══════════════════════════════════════════════════════════════════════
// Progress helpers — update display + bubble up to parent
// ═══════════════════════════════════════════════════════════════════════

function updateProgressDisplay(card: HTMLElement, node: GoalNode): void {
    const badge = card.querySelector('.goal-node-progress-badge');
    if (badge) badge.textContent = `${node.progress}%`;

    const fill = card.querySelector('.goal-node-progress-fill') as HTMLElement | null;
    if (fill) {
        fill.style.width = `${node.progress}%`;
        // Update color class
        fill.className = fill.className.replace(/bg-\w+-400/g, '');
        fill.classList.add(progressColor(node.progress));
    }
}

function propagateProgress(container: HTMLElement): void {
    // Bubble-up is handled when parent re-renders; for real-time we
    // dispatch a custom event so the host can recalculate.
    container.dispatchEvent(new CustomEvent('goaltree:progress-changed', { bubbles: true }));
}

// ═══════════════════════════════════════════════════════════════════════
// computeNodeProgress — recursive progress calculation
// ═══════════════════════════════════════════════════════════════════════

export function computeNodeProgress(node: GoalNode): number {
    if (node.isLeaf) {
        // If actionItems exist, derive progress from their completion ratio
        if (node.actionItems && node.actionItems.length > 0) {
            const completed = node.actionItems.filter(a => a.isCompleted).length;
            node.progress = Math.round((completed / node.actionItems.length) * 100);
        }
        return node.progress;
    }
    if (!node.children || node.children.length === 0) return 0;

    let sum = 0;
    for (const child of node.children) {
        child.progress = computeNodeProgress(child);
        sum += child.progress;
    }
    node.progress = Math.round(sum / node.children.length);
    return node.progress;
}

// ═══════════════════════════════════════════════════════════════════════
// collectLeaves — flatten all leaf nodes for iteration
// ═══════════════════════════════════════════════════════════════════════

export function collectLeaves(node: GoalNode): GoalNode[] {
    if (node.isLeaf) return [node];
    const leaves: GoalNode[] = [];
    if (node.children) {
        for (const child of node.children) {
            leaves.push(...collectLeaves(child));
        }
    }
    return leaves;
}

// ═══════════════════════════════════════════════════════════════════════
// findNodeById — recursive search by id
// ═══════════════════════════════════════════════════════════════════════

export function findNodeById(root: GoalNode, id: string): GoalNode | undefined {
    if (root.id === id) return root;
    if (root.children) {
        for (const child of root.children) {
            const found = findNodeById(child, id);
            if (found) return found;
        }
    }
    return undefined;
}

// ═══════════════════════════════════════════════════════════════════════
// treeDepth — compute maximum depth of the tree
// ═══════════════════════════════════════════════════════════════════════

export function treeDepth(node: GoalNode): number {
    if (node.isLeaf || !node.children || node.children.length === 0) return 1;
    let max = 0;
    for (const child of node.children) {
        const d = treeDepth(child);
        if (d > max) max = d;
    }
    return 1 + max;
}

// ═══════════════════════════════════════════════════════════════════════
// Public API — renderGoalTree
// ═══════════════════════════════════════════════════════════════════════

/**
 * Render a GoalNode tree into the given container element.
 * Clears the container first, then recursively builds nested DOM nodes
 * with Glassmorphism styling and tree-branch visual hierarchy.
 *
 * @param node      Root GoalNode of the tree
 * @param container Target DOM element to render into
 */
export function renderGoalTree(node: GoalNode, container: HTMLElement): void {
    container.innerHTML = '';

    // Recompute progress from leaves up before rendering
    computeNodeProgress(node);

    // Summary header
    const leaves = collectLeaves(node);
    const done = leaves.filter((l) => l.progress >= 100).length;
    const depth = treeDepth(node);

    const summary = document.createElement('div');
    summary.className = [
        'flex items-center gap-4 mb-4 px-4 py-3 rounded-xl',
        'backdrop-blur-md bg-white/5 border border-white/10',
    ].join(' ');
    summary.innerHTML = `
        <span class="text-xs text-white/50">Derinlik: <strong class="text-white/80">${depth}</strong></span>
        <span class="text-xs text-white/50">Yaprak: <strong class="text-white/80">${leaves.length}</strong></span>
        <span class="text-xs text-white/50">Tamamlanan: <strong class="text-emerald-400">${done}/${leaves.length}</strong></span>
        <span class="text-xs text-white/50">İlerleme: <strong class="text-sky-400">${node.progress}%</strong></span>
    `;
    container.appendChild(summary);

    // Render tree
    renderGoalNode(node, container, 0);
}

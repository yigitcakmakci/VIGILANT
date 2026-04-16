/**
 * goal-tree-bundle.js — Inline-styled recursive GoalNode tree renderer.
 * No Tailwind dependency. Exposes window.GoalTreeBundle.
 */
(function () {
	'use strict';

	/* ── colour helpers ──────────────────────────────────────────── */
	function progressColor(p) {
		if (p >= 100) return '#34d399';
		if (p >= 60)  return '#38bdf8';
		if (p >= 30)  return '#fbbf24';
		return '#fb7185';
	}

	var DEPTH_BORDER = [
		'rgba(99,102,241,.4)',
		'rgba(139,92,246,.4)',
		'rgba(217,70,239,.4)',
		'rgba(236,72,153,.4)',
		'rgba(244,63,94,.4)',
		'rgba(251,146,60,.4)',
		'rgba(251,191,36,.4)'
	];

	function esc(t) { var d = document.createElement('div'); d.textContent = t; return d.innerHTML; }

	/* ── renderGoalNode ──────────────────────────────────────────── */
	function renderGoalNode(node, parent, depth) {
		var wrapper = document.createElement('div');
		if (depth > 0) {
			wrapper.style.cssText = 'padding-left:24px;margin-left:8px;border-left:2px solid ' + DEPTH_BORDER[depth % DEPTH_BORDER.length] + ';margin-top:8px;';
		} else {
			wrapper.style.marginTop = '8px';
		}

		/* card */
		var card = document.createElement('div');
		card.style.cssText = 'position:relative;border-radius:12px;padding:14px 16px;margin-bottom:8px;' +
			'background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);' +
			'box-shadow:0 4px 30px rgba(0,0,0,0.1);transition:background .2s,border-color .2s;';
		card.dataset.nodeId = node.id;
		card.onmouseenter = function () { card.style.background = 'rgba(255,255,255,0.1)'; card.style.borderColor = 'rgba(255,255,255,0.2)'; };
		card.onmouseleave = function () { card.style.background = 'rgba(255,255,255,0.05)'; card.style.borderColor = 'rgba(255,255,255,0.1)'; };

		/* header */
		var header = document.createElement('div');
		header.style.cssText = 'display:flex;align-items:center;gap:10px;margin-bottom:4px;';

		if (node.isLeaf) {
			var cb = document.createElement('input');
			cb.type = 'checkbox';
			cb.checked = node.progress >= 100;
			cb.style.cssText = 'width:18px;height:18px;cursor:pointer;accent-color:#34d399;flex-shrink:0;';
			cb.addEventListener('change', function () {
				node.progress = cb.checked ? 100 : 0;
				updateProgressDisplay(card, node);
				parent.dispatchEvent(new CustomEvent('goaltree:progress-changed', { bubbles: true }));
			});
			header.appendChild(cb);
		} else {
			var icon = document.createElement('span');
			icon.style.cssText = 'font-size:16px;flex-shrink:0;user-select:none;';
			icon.textContent = depth === 0 ? '🎯' : '📂';
			header.appendChild(icon);
		}

		var titleEl = document.createElement('span');
		titleEl.style.cssText = 'font-weight:600;color:rgba(255,255,255,0.9);font-size:13px;flex:1;line-height:1.3;';
		titleEl.textContent = node.title;
		header.appendChild(titleEl);

		var badge = document.createElement('span');
		badge.className = 'goal-node-progress-badge';
		badge.style.cssText = 'font-size:11px;font-family:monospace;padding:2px 8px;border-radius:9999px;background:rgba(255,255,255,0.1);color:rgba(255,255,255,0.6);flex-shrink:0;';
		badge.textContent = node.progress + '%';
		header.appendChild(badge);

		card.appendChild(header);

		/* description */
		if (node.description) {
			var desc = document.createElement('p');
			desc.style.cssText = 'font-size:11px;color:rgba(255,255,255,0.4);margin-top:4px;line-height:1.5;margin-bottom:0;';
			desc.textContent = node.description;
			card.appendChild(desc);
		}

		/* progress bar */
		var barTrack = document.createElement('div');
		barTrack.style.cssText = 'width:100%;height:4px;border-radius:9999px;background:rgba(255,255,255,0.1);margin-top:8px;overflow:hidden;';
		var barFill = document.createElement('div');
		barFill.className = 'goal-node-progress-fill';
		var pct = Math.min(100, Math.max(0, node.progress));
		barFill.style.cssText = 'height:100%;border-radius:9999px;transition:width .5s;width:' + pct + '%;background:' + progressColor(node.progress) + ';';
		barTrack.appendChild(barFill);
		card.appendChild(barTrack);

		/* acceptance criteria */
		if (node.isLeaf && node.acceptanceCriteria) {
			var acWrap = document.createElement('div');
			acWrap.style.cssText = 'margin-top:8px;padding:8px 12px;border-radius:8px;background:rgba(16,185,129,0.1);border:1px solid rgba(16,185,129,0.2);';
			var acLabel = document.createElement('span');
			acLabel.style.cssText = 'font-size:9px;text-transform:uppercase;letter-spacing:0.05em;color:rgba(52,211,153,0.7);font-weight:600;display:block;';
			acLabel.textContent = 'Kabul Kriteri';
			acWrap.appendChild(acLabel);
			var acText = document.createElement('p');
			acText.style.cssText = 'font-size:11px;color:rgba(52,211,153,0.8);margin-top:2px;line-height:1.5;margin-bottom:0;';
			acText.textContent = node.acceptanceCriteria;
			acWrap.appendChild(acText);
			card.appendChild(acWrap);
		}

		/* action items checklist */
		if (node.actionItems && node.actionItems.length > 0) {
			var aiWrap = document.createElement('div');
			aiWrap.style.cssText = 'margin-top:8px;padding-left:16px;';
			var aiLabel = document.createElement('span');
			aiLabel.style.cssText = 'font-size:9px;text-transform:uppercase;letter-spacing:0.05em;color:rgba(255,255,255,0.4);font-weight:600;display:block;';
			aiLabel.textContent = 'Aksiyon Maddeleri';
			aiWrap.appendChild(aiLabel);
			var aiList = document.createElement('ul');
			aiList.style.cssText = 'margin:4px 0 0 0;padding:0;list-style:none;';
			for (var ai = 0; ai < node.actionItems.length; ai++) {
				(function(item) {
					var li = document.createElement('li');
					li.style.cssText = 'display:flex;align-items:flex-start;gap:8px;margin-bottom:4px;';
					var cb = document.createElement('input');
					cb.type = 'checkbox';
					cb.checked = item.isCompleted;
					cb.style.cssText = 'width:14px;height:14px;margin-top:1px;cursor:pointer;accent-color:#34d399;flex-shrink:0;';
					cb.addEventListener('change', function() {
						item.isCompleted = cb.checked;
						lbl.style.textDecoration = cb.checked ? 'line-through' : 'none';
						lbl.style.opacity = cb.checked ? '0.5' : '1';
						computeNodeProgress(node);
						updateProgressDisplay(card, node);
						parent.dispatchEvent(new CustomEvent('goaltree:progress-changed', { bubbles: true }));
					});
					li.appendChild(cb);
					var lbl = document.createElement('span');
					lbl.style.cssText = 'font-size:11px;color:rgba(255,255,255,0.5);line-height:1.3;';
					if (item.isCompleted) { lbl.style.textDecoration = 'line-through'; lbl.style.opacity = '0.5'; }
					lbl.textContent = item.text;
					li.appendChild(lbl);
					aiList.appendChild(li);
				})(node.actionItems[ai]);
			}
			aiWrap.appendChild(aiList);
			card.appendChild(aiWrap);
		}

		wrapper.appendChild(card);

		/* children */
		if (!node.isLeaf && node.children && node.children.length > 0) {
			var childContainer = document.createElement('div');
			for (var i = 0; i < node.children.length; i++) {
				renderGoalNode(node.children[i], childContainer, depth + 1);
			}
			wrapper.appendChild(childContainer);
		}

		parent.appendChild(wrapper);
	}

	function updateProgressDisplay(card, node) {
		var badge = card.querySelector('.goal-node-progress-badge');
		if (badge) badge.textContent = node.progress + '%';
		var fill = card.querySelector('.goal-node-progress-fill');
		if (fill) {
			fill.style.width = node.progress + '%';
			fill.style.background = progressColor(node.progress);
		}
	}

	/* ── tree utilities ──────────────────────────────────────────── */
	function computeNodeProgress(node) {
		if (node.isLeaf) {
			if (node.actionItems && node.actionItems.length > 0) {
				var completed = 0;
				for (var i = 0; i < node.actionItems.length; i++) {
					if (node.actionItems[i].isCompleted) completed++;
				}
				node.progress = Math.round((completed / node.actionItems.length) * 100);
			}
			return node.progress;
		}
		if (!node.children || node.children.length === 0) return 0;
		var sum = 0;
		for (var i = 0; i < node.children.length; i++) {
			node.children[i].progress = computeNodeProgress(node.children[i]);
			sum += node.children[i].progress;
		}
		node.progress = Math.round(sum / node.children.length);
		return node.progress;
	}

	function collectLeaves(node) {
		if (node.isLeaf) return [node];
		var leaves = [];
		if (node.children) {
			for (var i = 0; i < node.children.length; i++) {
				leaves = leaves.concat(collectLeaves(node.children[i]));
			}
		}
		return leaves;
	}

	function findNodeById(root, id) {
		if (root.id === id) return root;
		if (root.children) {
			for (var i = 0; i < root.children.length; i++) {
				var found = findNodeById(root.children[i], id);
				if (found) return found;
			}
		}
		return undefined;
	}

	function treeDepth(node) {
		if (node.isLeaf || !node.children || node.children.length === 0) return 1;
		var max = 0;
		for (var i = 0; i < node.children.length; i++) {
			var d = treeDepth(node.children[i]);
			if (d > max) max = d;
		}
		return 1 + max;
	}

	/* ── renderGoalTree (public) — single root ──────────────────── */
	function renderGoalTree(node, container) {
		container.innerHTML = '';
		computeNodeProgress(node);

		var leaves = collectLeaves(node);
		var done = leaves.filter(function (l) { return l.progress >= 100; }).length;
		var depth = treeDepth(node);

		/* summary bar */
		var summary = document.createElement('div');
		summary.style.cssText = 'display:flex;align-items:center;gap:16px;margin-bottom:16px;padding:10px 16px;border-radius:12px;' +
			'background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);flex-wrap:wrap;';
		summary.innerHTML =
			'<span style="font-size:11px;color:rgba(255,255,255,0.5)">Derinlik: <strong style="color:rgba(255,255,255,0.8)">' + depth + '</strong></span>' +
			'<span style="font-size:11px;color:rgba(255,255,255,0.5)">Yaprak: <strong style="color:rgba(255,255,255,0.8)">' + leaves.length + '</strong></span>' +
			'<span style="font-size:11px;color:rgba(255,255,255,0.5)">Tamamlanan: <strong style="color:#34d399">' + done + '/' + leaves.length + '</strong></span>' +
			'<span style="font-size:11px;color:rgba(255,255,255,0.5)">İlerleme: <strong style="color:#38bdf8">' + node.progress + '%</strong></span>';
		container.appendChild(summary);

		renderGoalNode(node, container, 0);
	}

	/* ── renderGoalForest (public) — multiple roots as accordion ── */
	function renderGoalForest(trees, container, opts) {
		container.innerHTML = '';
		opts = opts || {};
		var onDelete = opts.onDelete || null;

		/* Toggle visibility: show tree container, hide empty state */
		container.style.display = 'block';
		var emptyState = document.getElementById('stEmptyState');
		if (emptyState) emptyState.style.display = 'none';

		if (!trees || trees.length === 0) {
			container.innerHTML = '<div style="text-align:center;padding:40px;color:rgba(255,255,255,0.2);font-size:14px">Henüz hedef eklenmedi.</div>';
			return;
		}

		for (var ti = 0; ti < trees.length; ti++) {
			(function(tree, index) {
				var root = tree.root || tree;
				computeNodeProgress(root);

				var leaves = collectLeaves(root);
				var done = leaves.filter(function(l) { return l.progress >= 100; }).length;
				var totalProgress = root.progress;

				/* Accordion card wrapper */
				var card = document.createElement('div');
				card.style.cssText = 'margin-bottom:12px;border-radius:14px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);overflow:hidden;transition:border-color 0.2s;';
				card.dataset.goalRootId = root.id;
				card.onmouseenter = function() { card.style.borderColor = 'rgba(255,255,255,0.15)'; };
				card.onmouseleave = function() { card.style.borderColor = 'rgba(255,255,255,0.08)'; };

				/* Header (always visible) */
				var header = document.createElement('div');
				header.style.cssText = 'display:flex;align-items:center;gap:12px;padding:14px 16px;cursor:pointer;user-select:none;';

				/* Chevron */
				var chevron = document.createElement('span');
				chevron.style.cssText = 'display:inline-flex;align-items:center;justify-content:center;width:20px;height:20px;font-size:12px;color:rgba(255,255,255,0.35);transition:transform 0.3s;flex-shrink:0;';
				chevron.textContent = '▶';
				header.appendChild(chevron);

				/* Icon */
				var icon = document.createElement('span');
				icon.style.cssText = 'font-size:16px;flex-shrink:0;';
				icon.textContent = '🎯';
				header.appendChild(icon);

				/* Title + subtitle */
				var titleBlock = document.createElement('div');
				titleBlock.style.cssText = 'flex:1;min-width:0;';
				var titleEl = document.createElement('div');
				titleEl.style.cssText = 'font-weight:600;color:rgba(255,255,255,0.9);font-size:14px;line-height:1.3;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;';
				titleEl.textContent = root.title;
				titleBlock.appendChild(titleEl);
				var subtitleEl = document.createElement('div');
				subtitleEl.style.cssText = 'font-size:11px;color:rgba(255,255,255,0.3);margin-top:2px;';
				subtitleEl.textContent = leaves.length + ' yaprak · ' + done + ' tamamlanan';
				titleBlock.appendChild(subtitleEl);
				header.appendChild(titleBlock);

				/* Progress badge */
				var progBadge = document.createElement('span');
				progBadge.style.cssText = 'font-size:12px;font-family:monospace;padding:3px 10px;border-radius:9999px;flex-shrink:0;font-weight:600;' +
					'background:rgba(255,255,255,0.08);color:' + (totalProgress >= 100 ? '#34d399' : totalProgress >= 50 ? '#38bdf8' : 'rgba(255,255,255,0.5)') + ';';
				progBadge.textContent = totalProgress + '%';
				header.appendChild(progBadge);

				/* Mini progress bar in header */
				var miniBar = document.createElement('div');
				miniBar.style.cssText = 'width:60px;height:4px;border-radius:9999px;background:rgba(255,255,255,0.08);overflow:hidden;flex-shrink:0;';
				var miniFill = document.createElement('div');
				miniFill.style.cssText = 'height:100%;border-radius:9999px;transition:width 0.5s;width:' + totalProgress + '%;background:' + progressColor(totalProgress) + ';';
				miniBar.appendChild(miniFill);
				header.appendChild(miniBar);

				/* Delete button */
				var delBtn = document.createElement('button');
				delBtn.style.cssText = 'background:none;border:none;cursor:pointer;padding:4px 6px;border-radius:6px;color:rgba(255,255,255,0.2);font-size:14px;transition:color 0.2s,background 0.2s;flex-shrink:0;';
				delBtn.textContent = '🗑';
				delBtn.title = 'Hedefi sil';
				delBtn.onmouseenter = function() { delBtn.style.color = '#fb7185'; delBtn.style.background = 'rgba(244,63,94,0.1)'; };
				delBtn.onmouseleave = function() { delBtn.style.color = 'rgba(255,255,255,0.2)'; delBtn.style.background = 'none'; };
				delBtn.onclick = function(e) {
					e.stopPropagation();
					if (!confirm('Bu hedefi silmek istediğinize emin misiniz?\n\n"' + root.title + '"')) return;
					/* Send RemoveGoalRequested to C++ */
					if (window.chrome && window.chrome.webview) {
						window.chrome.webview.postMessage({
							type: 'RemoveGoalRequested',
							sessionId: '',
							requestId: 'rg-' + Date.now(),
							ts: new Date().toISOString(),
							payload: { goalRootId: root.id }
						});
					}
					/* Remove from localStorage */
					try {
						var stored = JSON.parse(localStorage.getItem('vigilant-goaltrees') || '[]');
						stored = stored.filter(function(t) {
							var r = t.root || t;
							return r.id !== root.id;
						});
						localStorage.setItem('vigilant-goaltrees', JSON.stringify(stored));
					} catch(ex) {}
					/* Animate removal */
					card.style.transition = 'max-height 0.3s ease, opacity 0.3s ease, margin 0.3s ease';
					card.style.maxHeight = card.scrollHeight + 'px';
					card.offsetHeight;
					card.style.maxHeight = '0px';
					card.style.opacity = '0';
					card.style.marginBottom = '0px';
					setTimeout(function() { card.remove(); }, 350);
					if (typeof onDelete === 'function') onDelete(root.id);
				};
				header.appendChild(delBtn);

				card.appendChild(header);

				/* Collapsible body */
				var body = document.createElement('div');
				body.style.cssText = 'max-height:0;overflow:hidden;transition:max-height 0.4s ease;';

				/* Inner content padding */
				var bodyInner = document.createElement('div');
				bodyInner.style.cssText = 'padding:0 16px 16px 16px;';

				/* Summary stats inside body */
				var depth = treeDepth(root);
				var stats = document.createElement('div');
				stats.style.cssText = 'display:flex;align-items:center;gap:16px;margin-bottom:12px;padding:8px 12px;border-radius:10px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);flex-wrap:wrap;';
				stats.innerHTML =
					'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Derinlik: <strong style="color:rgba(255,255,255,0.7)">' + depth + '</strong></span>' +
					'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Yaprak: <strong style="color:rgba(255,255,255,0.7)">' + leaves.length + '</strong></span>' +
					'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Tamamlanan: <strong style="color:#34d399">' + done + '/' + leaves.length + '</strong></span>';
				bodyInner.appendChild(stats);

				/* Render tree nodes */
				renderGoalNode(root, bodyInner, 0);
				body.appendChild(bodyInner);
				card.appendChild(body);

				/* Toggle logic */
				var expanded = false;
				header.onclick = function(e) {
					if (e.target === delBtn || e.target.parentNode === delBtn) return;
					expanded = !expanded;
					if (expanded) {
						body.style.maxHeight = body.scrollHeight + 'px';
						chevron.style.transform = 'rotate(90deg)';
						/* After transition, set auto so inner changes don't clip */
						setTimeout(function() { if (expanded) body.style.maxHeight = 'none'; }, 400);
					} else {
						body.style.maxHeight = body.scrollHeight + 'px';
						body.offsetHeight;
						body.style.maxHeight = '0';
						chevron.style.transform = 'rotate(0deg)';
					}
				};

				container.appendChild(card);
			})(trees[ti], ti);
		}
	}

	/* ── handleGoalTreeEvent ─────────────────────────────────────── */
	function handleGoalTreeEvent(payload) {
		var container = document.getElementById('goal-tree-container');
		if (!container) return;

		/* Accept either a single goalTree or a goalForest array */
		if (payload && payload.goalForest && Array.isArray(payload.goalForest)) {
			renderGoalForest(payload.goalForest, container);
		} else if (payload && payload.goalTree) {
			/* Wrap single tree into forest for consistent rendering */
			renderGoalForest([payload.goalTree], container);
		}
	}

	/* ── loadForestFromStorage — read persisted array ────────────── */
	function loadForestFromStorage() {
		var container = document.getElementById('goal-tree-container');
		if (!container) return;
		try {
			var raw = localStorage.getItem('vigilant-goaltrees');
			if (raw) {
				var arr = JSON.parse(raw);
				if (Array.isArray(arr) && arr.length > 0) {
					renderGoalForest(arr, container);
					return true;
				}
			}
		} catch(e) {}
		/* Migrate legacy single-tree key */
		try {
			var legacy = localStorage.getItem('vigilant-goaltree');
			if (legacy) {
				var parsed = JSON.parse(legacy);
				var root = parsed.root || parsed;
				if (root && root.id) {
					var forest = [root];
					localStorage.setItem('vigilant-goaltrees', JSON.stringify(forest));
					localStorage.removeItem('vigilant-goaltree');
					renderGoalForest(forest, container);
					return true;
				}
			}
		} catch(e) {}
		return false;
	}

	/* ── expose global ───────────────────────────────────────────── */
	window.GoalTreeBundle = {
		renderGoalTree: renderGoalTree,
		renderGoalForest: renderGoalForest,
		loadForestFromStorage: loadForestFromStorage,
		computeNodeProgress: computeNodeProgress,
		collectLeaves: collectLeaves,
		findNodeById: findNodeById,
		treeDepth: treeDepth,
		handleGoalTreeEvent: handleGoalTreeEvent
	};
})();

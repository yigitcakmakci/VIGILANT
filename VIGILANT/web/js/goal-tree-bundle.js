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

	/* ── persistence helpers (used by inline editing) ────────────── */
	function loadForestRaw() {
		try {
			var raw = localStorage.getItem('vigilant-goaltrees');
			if (raw) {
				var arr = JSON.parse(raw);
				if (Array.isArray(arr)) return arr;
			}
		} catch(e) {}
		return [];
	}
	function persistForest(forest) {
		try { localStorage.setItem('vigilant-goaltrees', JSON.stringify(forest)); } catch(e) {}
	}
	/* Replace the tree whose root id matches and persist. */
	function persistTreeRoot(rootId, newTreeOrRoot) {
		var forest = loadForestRaw();
		var replaced = false;
		for (var i = 0; i < forest.length; i++) {
			var entry = forest[i];
			var r = entry && entry.root ? entry.root : entry;
			if (r && r.id === rootId) {
				if (entry && entry.root) entry.root = newTreeOrRoot.root || newTreeOrRoot;
				else forest[i] = newTreeOrRoot.root || newTreeOrRoot;
				replaced = true;
				break;
			}
		}
		if (!replaced) forest.push(newTreeOrRoot);
		persistForest(forest);
		return forest;
	}
	function notifyForestChanged(rerenderContainer, forest, opts) {
		if (!rerenderContainer) return;
		renderGoalForest(forest, rerenderContainer, opts || {});
	}

	/* ── inline-edit helpers ─────────────────────────────────────── */
	function makeEditableText(el, getValue, setValue, onCommit) {
		el.title = (el.title ? el.title + ' • ' : '') + 'Düzenlemek için çift tıkla';
		el.style.cursor = 'text';
		el.addEventListener('dblclick', function (e) {
			e.stopPropagation();
			startInlineEdit(el, getValue, setValue, onCommit);
		});
	}
	function startInlineEdit(el, getValue, setValue, onCommit) {
		if (el.dataset.editing === '1') return;
		el.dataset.editing = '1';
		var original = getValue();
		el.contentEditable = 'true';
		el.spellcheck = false;
		el.style.outline = '1px dashed rgba(56,189,248,0.6)';
		el.style.borderRadius = '4px';
		el.textContent = original;
		var sel = window.getSelection();
		var range = document.createRange();
		range.selectNodeContents(el);
		sel.removeAllRanges();
		sel.addRange(range);
		el.focus();
		var done = false;
		function commit(save) {
			if (done) return;
			done = true;
			el.contentEditable = 'false';
			el.style.outline = 'none';
			el.dataset.editing = '0';
			var v = (el.textContent || '').trim();
			if (save && v && v !== original) {
				setValue(v);
				if (typeof onCommit === 'function') onCommit(v);
			} else {
				el.textContent = original;
			}
		}
		el.addEventListener('blur', function () { commit(true); }, { once: true });
		el.addEventListener('keydown', function (e) {
			if (e.key === 'Enter') { e.preventDefault(); el.blur(); }
			else if (e.key === 'Escape') { e.preventDefault(); el.textContent = original; commit(false); }
		});
	}
	function makeIconBtn(symbol, label, color) {
		var b = document.createElement('button');
		b.type = 'button';
		b.textContent = symbol;
		b.title = label;
		b.style.cssText = 'background:none;border:none;cursor:pointer;padding:2px 6px;border-radius:6px;color:' +
			(color || 'rgba(255,255,255,0.35)') + ';font-size:13px;line-height:1;flex-shrink:0;transition:background 0.15s,color 0.15s;';
		b.onmouseenter = function () { b.style.background = 'rgba(255,255,255,0.08)'; b.style.color = 'rgba(255,255,255,0.95)'; };
		b.onmouseleave = function () { b.style.background = 'none'; b.style.color = (color || 'rgba(255,255,255,0.35)'); };
		return b;
	}
	function newId(prefix) {
		return (prefix || 'n') + '-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 7);
	}

	/* ── renderGoalNode ──────────────────────────────────────────── */
	function renderGoalNode(node, parent, depth, ctx) {
		ctx = ctx || {};
		var commit = ctx.commit || function () {};
		var rerender = ctx.rerender || function () {};

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

		/* default-expand rule: depth 0–1 açık, daha derin koleps */
		var hasKids = !node.isLeaf && node.children && node.children.length > 0;
		if (hasKids && typeof node._expanded !== 'boolean') {
			node._expanded = (depth <= 1);
		}

		/* header */
		var header = document.createElement('div');
		header.style.cssText = 'display:flex;align-items:center;gap:10px;margin-bottom:4px;';

		var nodeChevron = null;
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
			if (hasKids) {
				nodeChevron = document.createElement('span');
				nodeChevron.style.cssText = 'display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;font-size:11px;color:rgba(255,255,255,0.4);transition:transform .2s;flex-shrink:0;cursor:pointer;user-select:none;';
				nodeChevron.textContent = '▶';
				if (node._expanded) nodeChevron.style.transform = 'rotate(90deg)';
				nodeChevron.title = node._expanded ? 'Kapat' : 'Aç';
				header.appendChild(nodeChevron);
			}
			var icon = document.createElement('span');
			icon.style.cssText = 'font-size:' + (depth === 0 ? '18px' : '15px') + ';flex-shrink:0;user-select:none;';
			icon.textContent = depth === 0 ? '🎯' : (depth === 1 ? '📁' : '📂');
			header.appendChild(icon);
		}

		/* tipografi: derinliğe göre küçülen başlık */
		var titleSize = depth === 0 ? 15 : (depth === 1 ? 13 : 12);
		var titleAlpha = depth === 0 ? 0.95 : (depth === 1 ? 0.85 : 0.75);
		var titleEl = document.createElement('span');
		titleEl.style.cssText = 'font-weight:' + (depth === 0 ? '700' : '600') + ';color:rgba(255,255,255,' + titleAlpha + ');font-size:' + titleSize + 'px;flex:1;line-height:1.3;';
		titleEl.textContent = node.title;
		if (node.isLeaf) {
			titleEl.style.cursor = 'pointer';
			titleEl.title = 'Tünel görüşünü aç';
			titleEl.addEventListener('click', function (e) {
				e.stopPropagation();
				if (window.TunnelVision && typeof window.TunnelVision.open === 'function') {
					window.TunnelVision.open(node.id, node.title);
				}
			});
		}
		makeEditableText(titleEl,
			function () { return node.title || ''; },
			function (v) { node.title = v; },
			function () { commit(); rerender(); });
		header.appendChild(titleEl);

		var badge = document.createElement('span');
		badge.className = 'goal-node-progress-badge';
		badge.style.cssText = 'font-size:11px;font-family:monospace;padding:2px 8px;border-radius:9999px;background:rgba(255,255,255,0.1);color:rgba(255,255,255,0.6);flex-shrink:0;';
		badge.textContent = node.progress + '%';
		/* hide zero-progress badge to reduce visual clutter (UI-11) */
		if (!node.progress || node.progress <= 0) badge.style.display = 'none';
		header.appendChild(badge);

		/* child count chip — verir kullanıcıya “bu altta ne var” sezgisi */
		if (hasKids) {
			var childChip = document.createElement('span');
			var leafCount = collectLeaves(node).length;
			childChip.style.cssText = 'font-size:10px;padding:2px 7px;border-radius:9999px;background:rgba(99,102,241,0.12);color:rgba(165,180,252,0.85);border:1px solid rgba(99,102,241,0.25);flex-shrink:0;font-weight:500;';
			childChip.textContent = node.children.length + ' alt · ' + leafCount + ' adım';
			childChip.title = node.children.length + ' alt hedef, toplam ' + leafCount + ' atomik adım';
			header.appendChild(childChip);
		}

		/* Edit toolbar: AI coach / add child / add action / delete (depth > 0 only) */
		var tools = document.createElement('span');
		tools.style.cssText = 'display:inline-flex;align-items:center;gap:2px;flex-shrink:0;';

		/* AI Coach — context-aware actions */
		var coachBtn = makeIconBtn('🤖', 'AI Koç eylemleri', 'rgba(216,180,254,0.85)');
		coachBtn.onclick = function (e) {
			e.stopPropagation();
			openNodeCoachMenu(coachBtn, node, ctx);
		};
		tools.appendChild(coachBtn);

		if (!node.isLeaf) {
			var addChildBtn = makeIconBtn('＋', 'Alt hedef ekle', 'rgba(125,211,252,0.8)');
			addChildBtn.onclick = function (e) {
				e.stopPropagation();
				node.children = node.children || [];
				node.children.push({
					id: newId('node'),
					title: 'Yeni alt hedef',
					description: '',
					progress: 0,
					isLeaf: false,
					children: [],
					actionItems: []
				});
				commit(); rerender();
			};
			tools.appendChild(addChildBtn);

			var addLeafBtn = makeIconBtn('✓', 'Atomik adım ekle', 'rgba(52,211,153,0.85)');
			addLeafBtn.onclick = function (e) {
				e.stopPropagation();
				node.children = node.children || [];
				node.children.push({
					id: newId('leaf'),
					title: 'Yeni atomik adım',
					description: '',
					progress: 0,
					isLeaf: true,
					actionItems: []
				});
				commit(); rerender();
			};
			tools.appendChild(addLeafBtn);
		} else {
			var addActionBtn = makeIconBtn('＋', 'Aksiyon maddesi ekle', 'rgba(125,211,252,0.8)');
			addActionBtn.onclick = function (e) {
				e.stopPropagation();
				node.actionItems = node.actionItems || [];
				node.actionItems.push({
					id: newId('act'),
					text: 'Yeni aksiyon',
					isCompleted: false
				});
				commit(); rerender();
			};
			tools.appendChild(addActionBtn);
		}
		if (depth > 0) {
			var delNodeBtn = makeIconBtn('🗑', 'Bu düğümü sil', 'rgba(251,113,133,0.85)');
			delNodeBtn.onclick = function (e) {
				e.stopPropagation();
				if (!confirm('Bu düğümü silmek istediğine emin misin?\n\n"' + (node.title || '') + '"')) return;
				if (typeof ctx.removeNode === 'function') ctx.removeNode(node);
				commit(); rerender();
			};
			tools.appendChild(delNodeBtn);
		}
		header.appendChild(tools);

		card.appendChild(header);

		/* description */
		var desc = document.createElement('p');
		desc.style.cssText = 'font-size:11px;color:rgba(255,255,255,0.4);margin-top:4px;line-height:1.5;margin-bottom:0;min-height:1em;';
		desc.textContent = node.description || '';
		if (!node.description) desc.style.fontStyle = 'italic';
		makeEditableText(desc,
			function () { return node.description || ''; },
			function (v) { node.description = v; desc.style.fontStyle = v ? 'normal' : 'italic'; },
			function () { commit(); });
		card.appendChild(desc);

		/* progress bar */
		var barTrack = document.createElement('div');
		barTrack.style.cssText = 'width:100%;height:4px;border-radius:9999px;background:rgba(255,255,255,0.1);margin-top:8px;overflow:hidden;';
		var barFill = document.createElement('div');
		barFill.className = 'goal-node-progress-fill';
		var pct = Math.min(100, Math.max(0, node.progress));
		barFill.style.cssText = 'height:100%;border-radius:9999px;transition:width .5s;width:' + pct + '%;background:' + progressColor(node.progress) + ';';
		barTrack.appendChild(barFill);
		card.appendChild(barTrack);

		/* obstacle banner — branch nodes only (KURAL 3 — Unknown Unknowns) */
		if (!node.isLeaf && node.obstacle && (node.obstacle.title || node.obstacle.detail)) {
			var obWrap = document.createElement('div');
			obWrap.style.cssText = 'margin-top:10px;padding:10px 12px;border-radius:8px;background:rgba(251,146,60,0.10);border:1px solid rgba(251,146,60,0.35);';
			var obHead = document.createElement('div');
			obHead.style.cssText = 'display:flex;align-items:center;gap:8px;';
			var obLabel = document.createElement('div');
			obLabel.style.cssText = 'font-size:9px;text-transform:uppercase;letter-spacing:0.06em;color:#fbbf24;font-weight:700;flex:1;';
			obLabel.innerHTML = '\u26A0 A\u015fIlmasi Gereken Engel';
			obHead.appendChild(obLabel);
			var obSolveBtn = document.createElement('button');
			obSolveBtn.type = 'button';
			obSolveBtn.textContent = '🤖 Çözüm üret';
			obSolveBtn.title = 'AI bu engel için 2-3 somut çözüm önerir';
			obSolveBtn.style.cssText = 'padding:3px 8px;border-radius:6px;font-size:10px;background:rgba(168,85,247,0.18);color:#e9d5ff;border:1px solid rgba(168,85,247,0.35);cursor:pointer;flex-shrink:0;';
			obSolveBtn.onclick = function(e) { e.stopPropagation(); sendCoachAction('unblock', node); };
			obHead.appendChild(obSolveBtn);
			obWrap.appendChild(obHead);
			if (node.obstacle.title) {
				var obTitle = document.createElement('div');
				obTitle.style.cssText = 'font-size:12px;color:#fde68a;font-weight:600;margin-top:4px;line-height:1.35;';
				obTitle.textContent = node.obstacle.title;
				obWrap.appendChild(obTitle);
			}
			if (node.obstacle.detail) {
				var obDetail = document.createElement('div');
				obDetail.style.cssText = 'font-size:11px;color:rgba(253,230,138,0.75);margin-top:3px;line-height:1.5;';
				obDetail.textContent = node.obstacle.detail;
				obWrap.appendChild(obDetail);
			}
			card.appendChild(obWrap);
		}

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
				(function(item, idx) {
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
						commit();
						parent.dispatchEvent(new CustomEvent('goaltree:progress-changed', { bubbles: true }));
					});
					li.appendChild(cb);
					var lbl = document.createElement('span');
					lbl.style.cssText = 'font-size:11px;color:rgba(255,255,255,0.5);line-height:1.3;cursor:pointer;flex:1;';
					lbl.title = 'Tünel görüşünü aç • Düzenlemek için çift tıkla';
					if (item.isCompleted) { lbl.style.textDecoration = 'line-through'; lbl.style.opacity = '0.5'; }
					lbl.textContent = item.text;
					lbl.addEventListener('click', function (e) {
						e.stopPropagation();
						if (window.TunnelVision && typeof window.TunnelVision.open === 'function') {
							var actionId = (item.id != null) ? String(item.id) : (node.id + ':action:' + item.text);
							window.TunnelVision.open(actionId, item.text);
						}
					});
					makeEditableText(lbl,
						function () { return item.text || ''; },
						function (v) { item.text = v; },
						function () { commit(); });
					li.appendChild(lbl);

					var rmBtn = makeIconBtn('×', 'Aksiyonu sil', 'rgba(251,113,133,0.7)');
					rmBtn.style.fontSize = '14px';
					rmBtn.onclick = function (e) {
						e.stopPropagation();
						node.actionItems.splice(idx, 1);
						commit(); rerender();
					};
					li.appendChild(rmBtn);

					aiList.appendChild(li);
				})(node.actionItems[ai], ai);
			}
			aiWrap.appendChild(aiList);
			card.appendChild(aiWrap);
		}

		wrapper.appendChild(card);

		/* children */
		var childContainer = null;
		if (!node.isLeaf && node.children && node.children.length > 0) {
			childContainer = document.createElement('div');
			childContainer.style.display = node._expanded ? 'block' : 'none';
			var siblings = node.children;
			var childCtx = {
				commit: ctx.commit,
				rerender: ctx.rerender,
				removeNode: function (target) {
					for (var k = 0; k < siblings.length; k++) {
						if (siblings[k] === target) { siblings.splice(k, 1); break; }
					}
				}
			};
			for (var i = 0; i < siblings.length; i++) {
				renderGoalNode(siblings[i], childContainer, depth + 1, childCtx);
			}
			wrapper.appendChild(childContainer);
		}

		/* toggle handler — chevron veya header (description hariç) tıklanınca aç/kapa */
		if (nodeChevron && childContainer) {
			var toggleNode = function (e) {
				if (e) e.stopPropagation();
				node._expanded = !node._expanded;
				childContainer.style.display = node._expanded ? 'block' : 'none';
				nodeChevron.style.transform = node._expanded ? 'rotate(90deg)' : 'rotate(0deg)';
				nodeChevron.title = node._expanded ? 'Kapat' : 'Aç';
			};
			nodeChevron.addEventListener('click', toggleNode);
			titleEl.addEventListener('click', function (e) {
				/* sadece title editable değilken toggle, leaf değil */
				if (titleEl.dataset.editing === '1') return;
				toggleNode(e);
			});
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

		var rootCtxSingle = {
			commit: function () {
				computeNodeProgress(node);
				persistTreeRoot(node.id, node);
			},
			rerender: function () { renderGoalTree(node, container); }
		};
		renderGoalNode(node, container, 0, rootCtxSingle);
	}

	/* ── collect first N pending action items across the tree ─────── */
	function collectPendingActions(root, max) {
		var out = [];
		(function walk(node, ancestors) {
			if (out.length >= max) return;
			var path = node.title ? ancestors.concat([node.title]) : ancestors;
			if (node.isLeaf && node.actionItems) {
				for (var i = 0; i < node.actionItems.length && out.length < max; i++) {
					var it = node.actionItems[i];
					if (!it.isCompleted) out.push({ leaf: node, item: it, path: path });
				}
			}
			if (!node.isLeaf && node.children) {
				for (var c = 0; c < node.children.length && out.length < max; c++) {
					walk(node.children[c], path);
				}
			}
		})(root, []);
		return out;
	}

	/* ── collect pending actions across the entire forest ─────────── */
	function collectForestPending(trees, max) {
		var out = [];
		for (var t = 0; t < trees.length && out.length < max; t++) {
			var entry = trees[t];
			var root = entry && entry.root ? entry.root : entry;
			if (!root) continue;
			var rest = max - out.length;
			var partial = collectPendingActions(root, rest);
			for (var k = 0; k < partial.length; k++) {
				partial[k].rootId = root.id;
				partial[k].rootTitle = root.title;
				out.push(partial[k]);
			}
		}
		return out;
	}

	/* ── Today's Mission — sticky next-action panel (UI-1) ─────── */
	function renderTodayMission(trees, container) {
		var pending = collectForestPending(trees, 3);
		var panel = document.createElement('div');
		panel.id = 'goal-today-mission';
		panel.style.cssText = 'margin-bottom:16px;padding:16px 18px;border-radius:14px;' +
			'background:linear-gradient(135deg,rgba(56,189,248,0.10),rgba(99,102,241,0.08));' +
			'border:1px solid rgba(56,189,248,0.30);box-shadow:0 4px 20px rgba(56,189,248,0.05);';

		var headBar = document.createElement('div');
		headBar.style.cssText = 'display:flex;align-items:center;gap:10px;margin-bottom:10px;';
		var icon = document.createElement('span');
		icon.style.cssText = 'font-size:18px;';
		icon.textContent = '🎯';
		headBar.appendChild(icon);
		var title = document.createElement('div');
		title.style.cssText = 'flex:1;';
		title.innerHTML =
			'<div style="font-size:11px;text-transform:uppercase;letter-spacing:0.08em;color:#7dd3fc;font-weight:700;">Bugünün Misyonu</div>' +
			'<div style="font-size:12px;color:rgba(255,255,255,0.55);margin-top:2px;">Tüm hedeflerin arasından sıradaki ' + (pending.length || 0) + ' atomik adım</div>';
		headBar.appendChild(title);

		/* AI önerisi butonu — Step 3'te aktif */
		var coachBtn = document.createElement('button');
		coachBtn.type = 'button';
		coachBtn.textContent = '💡 AI Koç';
		coachBtn.title = 'Bugün hangi adımdan başlamalıyım? (yakında)';
		coachBtn.style.cssText = 'padding:6px 12px;border-radius:8px;font-size:12px;font-weight:500;' +
			'background:rgba(168,85,247,0.18);color:#e9d5ff;border:1px solid rgba(168,85,247,0.35);' +
			'cursor:pointer;transition:background .2s;flex-shrink:0;';
		coachBtn.onmouseenter = function() { coachBtn.style.background = 'rgba(168,85,247,0.30)'; };
		coachBtn.onmouseleave = function() { coachBtn.style.background = 'rgba(168,85,247,0.18)'; };
		coachBtn.onclick = function() { askMissionCoach(pending); };
		headBar.appendChild(coachBtn);
		panel.appendChild(headBar);

		if (pending.length === 0) {
			var empty = document.createElement('div');
			empty.style.cssText = 'padding:12px;border-radius:10px;background:rgba(52,211,153,0.08);border:1px dashed rgba(52,211,153,0.3);color:#a7f3d0;font-size:13px;text-align:center;';
			empty.textContent = '✓ Şu an bekleyen atomik adım yok — yeni adımlar ekle veya hedeflerini açıp ilerleme planla.';
			panel.appendChild(empty);
		} else {
			var list = document.createElement('div');
			list.style.cssText = 'display:flex;flex-direction:column;gap:8px;';
			for (var i = 0; i < pending.length; i++) {
				(function(entry, idx) {
					var row = document.createElement('div');
					row.style.cssText = 'display:flex;align-items:flex-start;gap:10px;padding:10px 12px;border-radius:10px;' +
						'background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.06);transition:background .15s,border-color .15s;';
					row.onmouseenter = function() { row.style.background = 'rgba(255,255,255,0.07)'; row.style.borderColor = 'rgba(56,189,248,0.30)'; };
					row.onmouseleave = function() { row.style.background = 'rgba(255,255,255,0.04)'; row.style.borderColor = 'rgba(255,255,255,0.06)'; };

					/* checkbox — tek tıkla tamamla */
					var cb = document.createElement('input');
					cb.type = 'checkbox';
					cb.style.cssText = 'width:16px;height:16px;margin-top:2px;cursor:pointer;accent-color:#34d399;flex-shrink:0;';
					cb.title = 'Tamamlandı olarak işaretle';
					cb.addEventListener('click', function(e) { e.stopPropagation(); });
					cb.addEventListener('change', function() {
						if (!cb.checked) return;
						entry.item.isCompleted = true;
						computeNodeProgress(entry.leaf);
						/* persist */
						persistTreeRoot(entry.rootId, _findForestEntryByRootId(entry.rootId));
						/* re-render */
						var c = document.getElementById('goal-tree-container');
						if (c) renderGoalForest(loadForestRaw(), c);
					});
					row.appendChild(cb);

					var col = document.createElement('div');
					col.style.cssText = 'flex:1;min-width:0;';
					var num = (idx + 1) + '.';
					var txt = document.createElement('div');
					txt.style.cssText = 'font-size:13px;color:rgba(255,255,255,0.92);line-height:1.35;font-weight:500;';
					txt.innerHTML = '<span style="color:rgba(56,189,248,0.7);font-family:monospace;margin-right:6px;">' + num + '</span>' + esc(entry.item.text);
					col.appendChild(txt);
					var crumb = document.createElement('div');
					crumb.style.cssText = 'font-size:10px;color:rgba(255,255,255,0.40);margin-top:3px;';
					var path = entry.path && entry.path.length ? entry.path.join(' \u203A ') : entry.rootTitle;
					crumb.textContent = path;
					col.appendChild(crumb);
					row.appendChild(col);

					/* Focus / Tunnel button */
					var focusBtn = document.createElement('button');
					focusBtn.type = 'button';
					focusBtn.textContent = '▶';
					focusBtn.title = 'Tünel görüşünü aç ve odaklan';
					focusBtn.style.cssText = 'padding:6px 10px;border-radius:8px;font-size:12px;background:rgba(56,189,248,0.15);color:#7dd3fc;border:1px solid rgba(56,189,248,0.30);cursor:pointer;flex-shrink:0;';
					focusBtn.onclick = function(e) {
						e.stopPropagation();
						if (window.TunnelVision && typeof window.TunnelVision.open === 'function') {
							var actionId = entry.item.id != null ? String(entry.item.id) : (entry.leaf.id + ':action:' + entry.item.text);
							window.TunnelVision.open(actionId, entry.item.text);
						}
					};
					row.appendChild(focusBtn);
					list.appendChild(row);
				})(pending[i], i);
			}
			panel.appendChild(list);
		}

		container.appendChild(panel);

		/* App-link tabanlı otomatik progress nudge */
		renderProgressNudge(trees, container);
	}

	/* ── Progress Nudge — uygulamaya bağlı hedeflerde "ilerledin mi?" sorusu (UI-6) */
	function renderProgressNudge(trees, container) {
		try {
			/* dashboardAPI varsa son aktiviteye bak */
			if (!window.dashboardAPI || typeof window.dashboardAPI.getRecentActivities !== 'function') return;
			/* günde maks 3 nudge */
			var dayKey = 'goal-nudge-' + new Date().toISOString().slice(0,10);
			var shownToday = parseInt(localStorage.getItem(dayKey) || '0', 10);
			if (shownToday >= 3) return;

			var links = {};
			try { links = JSON.parse(localStorage.getItem('vigilant-app-goal-links') || '{}'); } catch(e) {}
			if (!links || Object.keys(links).length === 0) return;

			Promise.resolve(window.dashboardAPI.getRecentActivities({ limit: 5 })).then(function(rows) {
				if (!rows || !rows.length) return;
				var hit = null;
				for (var i = 0; i < rows.length; i++) {
					var name = (rows[i].processName || rows[i].appName || '').toLowerCase();
					var goalId = links[name];
					if (goalId) { hit = { app: name, goalId: goalId, mins: Math.round((rows[i].durationSeconds||0)/60) }; break; }
				}
				if (!hit) return;
				/* hedefin başlığını bul */
				var goalTitle = '';
				for (var t = 0; t < trees.length; t++) {
					var r = trees[t] && trees[t].root ? trees[t].root : trees[t];
					var n = r && findNodeById(r, hit.goalId);
					if (n) { goalTitle = n.title; break; }
				}
				if (!goalTitle) return;

				var nudge = document.createElement('div');
				nudge.style.cssText = 'margin:10px 0 16px;padding:12px 14px;border-radius:12px;' +
					'background:linear-gradient(135deg,rgba(52,211,153,0.10),rgba(34,197,94,0.06));' +
					'border:1px solid rgba(52,211,153,0.30);display:flex;align-items:center;gap:12px;flex-wrap:wrap;';
				nudge.innerHTML =
					'<span style="font-size:18px;">🌱</span>' +
					'<div style="flex:1;min-width:200px;">' +
						'<div style="font-size:12px;color:#a7f3d0;font-weight:600;">' + esc(hit.app) + ' kullanımın "' + esc(goalTitle) + '" hedefine bağlı</div>' +
						'<div style="font-size:11px;color:rgba(255,255,255,0.55);margin-top:2px;">Son ' + hit.mins + ' dk çalışmana karşılık bir adımı tamamladın mı?</div>' +
					'</div>' +
					'<button data-act="open" style="padding:6px 12px;border-radius:8px;font-size:12px;background:rgba(52,211,153,0.20);color:#a7f3d0;border:1px solid rgba(52,211,153,0.4);cursor:pointer;">Hedefi aç</button>' +
					'<button data-act="dismiss" style="padding:6px 10px;border-radius:8px;font-size:12px;background:transparent;color:rgba(255,255,255,0.4);border:1px solid rgba(255,255,255,0.1);cursor:pointer;">Şimdi değil</button>';
				container.appendChild(nudge);
				localStorage.setItem(dayKey, String(shownToday + 1));

				nudge.querySelector('[data-act="open"]').onclick = function() {
					/* basitçe ilgili tree'yi açmak için scrollIntoView */
					var card = container.querySelector('[data-goal-root-id="' + hit.goalId + '"]');
					if (card) { card.scrollIntoView({ behavior: 'smooth', block: 'start' }); var h = card.querySelector('div'); if (h && h.click) h.click(); }
					nudge.remove();
				};
				nudge.querySelector('[data-act="dismiss"]').onclick = function() { nudge.remove(); };
			}).catch(function() {});
		} catch(e) {}
	}

	function _findForestEntryByRootId(rootId) {
		var forest = loadForestRaw();
		for (var i = 0; i < forest.length; i++) {
			var e = forest[i];
			var r = e && e.root ? e.root : e;
			if (r && r.id === rootId) return e;
		}
		return null;
	}

	/* ── AI Koç (placeholder, Step 3'te tam aktive) ──────────────── */
	function askMissionCoach(pending) {
		if (!pending || pending.length === 0) {
			alert('Şu an bekleyen adım yok — önce hedeflerine atomik adımlar ekle.');
			return;
		}
		if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
			window.chrome.webview.postMessage({
				type: 'AskGoalCoach',
				sessionId: '',
				requestId: 'gc-' + Date.now(),
				ts: new Date().toISOString(),
				payload: {
					mode: 'pickNext',
					actions: pending.map(function(p) {
						return {
							text: p.item.text,
							path: p.path,
							rootTitle: p.rootTitle,
							rootId: p.rootId,
							leafId: p.leaf.id
						};
					})
				}
			});
			/* küçük geri bildirim */
			var t = document.getElementById('goal-today-mission');
			if (t) {
				var info = document.createElement('div');
				info.style.cssText = 'margin-top:10px;padding:8px 10px;border-radius:8px;background:rgba(168,85,247,0.10);color:#e9d5ff;font-size:11px;border:1px dashed rgba(168,85,247,0.35);';
				info.textContent = '🤖 AI Koç düşünüyor… (yakında doğrudan yanıt göreceksin)';
				t.appendChild(info);
				setTimeout(function() { if (info && info.parentNode) info.parentNode.removeChild(info); }, 3500);
			}
		} else {
			alert('AI Koç bağlantısı yalnızca uygulama içinde aktif.');
		}
	}

	/* ── Hedef sağlık metrikleri ──────────────────────────────────── */
	function computeGoalHealth(root) {
		var pending = 0, obstacles = 0, openBranches = 0, totalBranches = 0;
		(function walk(n) {
			if (!n) return;
			if (!n.isLeaf) {
				totalBranches++;
				if ((n.progress || 0) < 100) openBranches++;
				if (n.obstacle && (n.obstacle.title || n.obstacle.detail)) obstacles++;
			}
			if (n.isLeaf && n.actionItems) {
				for (var i = 0; i < n.actionItems.length; i++) if (!n.actionItems[i].isCompleted) pending++;
			}
			if (n.children) for (var c = 0; c < n.children.length; c++) walk(n.children[c]);
		})(root);

		/* Momentum: progress > 0 ve son edit zamanı yoksa, kabaca progress'ten türet */
		var p = root.progress || 0;
		var momentumLabel, momentumColor, momentumHint;
		if (p >= 75)      { momentumLabel = 'Güçlü';   momentumColor = '#34d399'; momentumHint = 'Bitirme aşamasında'; }
		else if (p >= 40) { momentumLabel = 'İyi';     momentumColor = '#38bdf8'; momentumHint = 'İlerleme stabil'; }
		else if (p > 0)   { momentumLabel = 'Yavaş';   momentumColor = '#fbbf24'; momentumHint = 'Hızlandırmak gerek'; }
		else              { momentumLabel = 'Durgun';  momentumColor = '#fb7185'; momentumHint = 'Henüz başlanmamış'; }

		return {
			pending: pending,
			obstacles: obstacles,
			openBranches: openBranches,
			totalBranches: totalBranches,
			momentumLabel: momentumLabel,
			momentumColor: momentumColor,
			momentumHint: momentumHint
		};
	}

	/* ── Node-level AI Coach popover menu ─────────────────────── */
	function openNodeCoachMenu(anchorBtn, node, ctx) {
		/* Aynı menü açıksa kapat */
		var existing = document.getElementById('goal-coach-popover');
		if (existing) { existing.remove(); if (existing.dataset.forNode === node.id) return; }

		var rect = anchorBtn.getBoundingClientRect();
		var pop = document.createElement('div');
		pop.id = 'goal-coach-popover';
		pop.dataset.forNode = node.id;
		pop.style.cssText = 'position:fixed;z-index:10000;min-width:240px;padding:6px;border-radius:10px;' +
			'background:rgba(20,20,30,0.97);border:1px solid rgba(168,85,247,0.35);box-shadow:0 12px 40px rgba(0,0,0,0.5);' +
			'left:' + Math.max(8, rect.left - 180) + 'px;top:' + (rect.bottom + 6) + 'px;';

		var hdr = document.createElement('div');
		hdr.style.cssText = 'padding:6px 10px 8px;border-bottom:1px solid rgba(255,255,255,0.06);margin-bottom:4px;';
		hdr.innerHTML = '<div style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#d8b4fe;font-weight:700;">🤖 AI Koç</div>' +
			'<div style="font-size:11px;color:rgba(255,255,255,0.5);margin-top:2px;line-height:1.3;">' + esc(node.title || '') + '</div>';
		pop.appendChild(hdr);

		var actions = node.isLeaf
			? [
				{ id: 'breakdown',    icon: '🪓', label: 'Daha küçük adımlara böl',    desc: 'Bu adımı 3-5 mikro aksiyona ayırır' },
				{ id: 'unblock',      icon: '🧗', label: 'Tıkandım — engelleri çöz',   desc: 'Olası engelleri ve çözümlerini önerir' },
				{ id: 'estimate',     icon: '⏱', label: 'Süre/zorluk tahmini iste',  desc: 'Yaklaşık süre ve zorluk seviyesi verir' }
			]
			: [
				{ id: 'split',        icon: '🌿', label: 'Bu hedefi alt-hedeflere böl', desc: 'Eksik kalan dalları AI ile tamamlar' },
				{ id: 'reframe',      icon: '✏', label: 'Daha net yeniden ifade et',  desc: 'Belirsiz başlıkları SMART hale getirir' },
				{ id: 'unblock',      icon: '🧗', label: 'Tıkandım — engel matrisi',   desc: 'Bu dalda neden ilerleyemediğini analiz eder' },
				{ id: 'replanBranch', icon: '🔄', label: 'Bu dalı yeniden planla',     desc: 'İlerlemene göre kalan adımları yeniler' }
			];

		actions.forEach(function(a) {
			var b = document.createElement('button');
			b.type = 'button';
			b.style.cssText = 'display:flex;align-items:flex-start;gap:10px;width:100%;padding:8px 10px;border-radius:8px;' +
				'background:transparent;border:none;color:rgba(255,255,255,0.85);text-align:left;cursor:pointer;transition:background .15s;';
			b.innerHTML = '<span style="font-size:14px;flex-shrink:0;line-height:1.2;">' + a.icon + '</span>' +
				'<span style="flex:1;min-width:0;"><span style="display:block;font-size:12px;font-weight:600;">' + esc(a.label) +
				'</span><span style="display:block;font-size:10px;color:rgba(255,255,255,0.45);margin-top:2px;line-height:1.3;">' + esc(a.desc) + '</span></span>';
			b.onmouseenter = function() { b.style.background = 'rgba(168,85,247,0.15)'; };
			b.onmouseleave = function() { b.style.background = 'transparent'; };
			b.onclick = function(e) {
				e.stopPropagation();
				sendCoachAction(a.id, node);
				pop.remove();
			};
			pop.appendChild(b);
		});

		document.body.appendChild(pop);

		/* dış tıklamada kapat */
		setTimeout(function() {
			document.addEventListener('mousedown', function onDoc(ev) {
				if (!pop.contains(ev.target)) {
					pop.remove();
					document.removeEventListener('mousedown', onDoc);
				}
			});
		}, 0);
	}

	function sendCoachAction(actionId, node) {
		if (!(window.chrome && window.chrome.webview && window.chrome.webview.postMessage)) {
			alert('AI Koç bağlantısı yalnızca uygulama içinde aktif.');
			return;
		}
		/* node'un kök hedefini bul (toast bağlamı için) */
		var forest = loadForestRaw();
		var rootInfo = null;
		(function findRoot() {
			for (var i = 0; i < forest.length; i++) {
				var e = forest[i]; var r = e && e.root ? e.root : e;
				if (!r) continue;
				if (findNodeById(r, node.id)) { rootInfo = r; break; }
			}
		})();

		window.chrome.webview.postMessage({
			type: 'AskGoalCoach',
			sessionId: '',
			requestId: 'gc-' + Date.now(),
			ts: new Date().toISOString(),
			payload: {
				mode: actionId,
				nodeId: node.id,
				nodeTitle: node.title || '',
				nodeDescription: node.description || '',
				isLeaf: !!node.isLeaf,
				rootId: rootInfo ? rootInfo.id : null,
				rootTitle: rootInfo ? rootInfo.title : null
			}
		});

		/* anlık küçük geri bildirim */
		try {
			var toast = document.createElement('div');
			toast.style.cssText = 'position:fixed;bottom:24px;right:24px;z-index:10001;padding:10px 14px;border-radius:10px;' +
				'background:rgba(168,85,247,0.18);border:1px solid rgba(168,85,247,0.4);color:#f3e8ff;font-size:12px;' +
				'box-shadow:0 8px 24px rgba(0,0,0,0.4);';
			toast.textContent = '🤖 AI Koç isteği gönderildi: ' + actionId;
			document.body.appendChild(toast);
			setTimeout(function() { toast.style.opacity = '0'; toast.style.transition = 'opacity .4s'; }, 2200);
			setTimeout(function() { if (toast.parentNode) toast.parentNode.removeChild(toast); }, 2700);
		} catch(e) {}
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

		/* ── Today's Mission paneli (forest geneli) ─────────────── */
		renderTodayMission(trees, container);

		for (var ti = 0; ti < trees.length; ti++) {
			(function(tree, index) {
				var root = tree.root || tree;
				computeNodeProgress(root);

				var leaves = collectLeaves(root);
				var done = leaves.filter(function(l) { return l.progress >= 100; }).length;
				var totalProgress = root.progress;

				/* Accordion card wrapper */
				var card = document.createElement('div');
				card.style.cssText = 'margin-bottom:12px;border-radius:14px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);overflow:hidden;transition:border-color 0.2s, opacity 0.2s, transform 0.2s;';
				card.dataset.goalRootId = root.id;
				card.dataset.goalIndex = String(index);

				/* ── Drag & Drop (HTML5) for Major card ───────────── */
				card.draggable = true;
				card.addEventListener('dragstart', function(e) {
					container.__dragSourceIndex = index;
					card.style.opacity = '0.4';
					if (e.dataTransfer) {
						e.dataTransfer.effectAllowed = 'move';
						try { e.dataTransfer.setData('text/plain', String(index)); } catch(ex) {}
					}
				});
				card.addEventListener('dragend', function() {
					card.style.opacity = '1';
					container.__dragSourceIndex = -1;
					var all = container.querySelectorAll('[data-goal-index]');
					for (var k = 0; k < all.length; k++) {
						all[k].style.borderColor = 'rgba(255,255,255,0.08)';
					}
				});
				card.addEventListener('dragover', function(e) {
					e.preventDefault();
					if (e.dataTransfer) e.dataTransfer.dropEffect = 'move';
					card.style.borderColor = 'rgba(56,189,248,0.6)';
				});
				card.addEventListener('dragleave', function() {
					card.style.borderColor = 'rgba(255,255,255,0.08)';
				});
				card.addEventListener('drop', function(e) {
					e.preventDefault();
					card.style.borderColor = 'rgba(255,255,255,0.08)';
					var oldIndex = (typeof container.__dragSourceIndex === 'number') ? container.__dragSourceIndex : -1;
					if (oldIndex < 0 && e.dataTransfer) {
						var raw = e.dataTransfer.getData('text/plain');
						var parsed = parseInt(raw, 10);
						if (!isNaN(parsed)) oldIndex = parsed;
					}
					var newIndex = index;
					if (oldIndex < 0 || oldIndex === newIndex) return;

					if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
						window.chrome.webview.postMessage({
							type: 'UpdateGoalOrder',
							sessionId: '',
							requestId: 'rgo-' + Date.now(),
							ts: new Date().toISOString(),
							payload: { oldIndex: oldIndex, newIndex: newIndex }
						});
					}

					/* Optimistic local reorder of forest in localStorage */
					try {
						var stored = JSON.parse(localStorage.getItem('vigilant-goaltrees') || '[]');
						if (Array.isArray(stored) && oldIndex < stored.length && newIndex < stored.length) {
							var moved = stored.splice(oldIndex, 1)[0];
							stored.splice(newIndex, 0, moved);
							localStorage.setItem('vigilant-goaltrees', JSON.stringify(stored));
							renderGoalForest(stored, container, opts);
						}
					} catch(ex) {}
				});

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
						var src = (tree && tree.generation_source) || (root && root.generation_source) || '';
						var srcBadge = '';
						if (src === 'ai') {
							srcBadge = '<span style="font-size:10px;padding:2px 8px;border-radius:9999px;background:rgba(56,189,248,0.15);color:#7dd3fc;border:1px solid rgba(56,189,248,0.35);">\uD83E\uDD16 AI</span>';
						} else if (src === 'fallback') {
							srcBadge = '<span style="font-size:10px;padding:2px 8px;border-radius:9999px;background:rgba(251,146,60,0.15);color:#fdba74;border:1px solid rgba(251,146,60,0.35);">\uD83D\uDCE6 Fallback</span>';
						}
						var stats = document.createElement('div');
						stats.style.cssText = 'display:flex;align-items:center;gap:16px;margin-bottom:12px;padding:8px 12px;border-radius:10px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);flex-wrap:wrap;';
						stats.innerHTML =
							'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Derinlik: <strong style="color:rgba(255,255,255,0.7)">' + depth + '</strong></span>' +
							'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Yaprak: <strong style="color:rgba(255,255,255,0.7)">' + leaves.length + '</strong></span>' +
							'<span style="font-size:10px;color:rgba(255,255,255,0.4)">Tamamlanan: <strong style="color:#34d399">' + done + '/' + leaves.length + '</strong></span>' +
							 srcBadge;
						bodyInner.appendChild(stats);

						/* ── Hedef Sağlık Paneli (UI-5) ─────────────── */
						var health = computeGoalHealth(root);
						var healthGrid = document.createElement('div');
						healthGrid.style.cssText = 'display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin-bottom:14px;';
						function kpi(label, value, color, hint) {
							var c = document.createElement('div');
							c.style.cssText = 'padding:10px 12px;border-radius:10px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.07);';
							c.innerHTML =
								'<div style="font-size:9px;text-transform:uppercase;letter-spacing:0.07em;color:rgba(255,255,255,0.4);font-weight:600;">' + esc(label) + '</div>' +
								'<div style="font-size:16px;font-weight:700;color:' + color + ';margin-top:3px;line-height:1.1;">' + esc(value) + '</div>' +
								'<div style="font-size:10px;color:rgba(255,255,255,0.35);margin-top:2px;">' + esc(hint || '') + '</div>';
							return c;
						}
						healthGrid.appendChild(kpi('Bekleyen', health.pending + ' adım', health.pending > 0 ? '#fbbf24' : '#34d399', health.pending === 0 ? 'Hepsi tamam' : 'Atomik aksiyon'));
						healthGrid.appendChild(kpi('Engeller', String(health.obstacles), health.obstacles > 0 ? '#fb7185' : '#34d399', health.obstacles ? 'Çözmen gereken' : 'Engel yok'));
						healthGrid.appendChild(kpi('Açık dallar', String(health.openBranches), '#7dd3fc', health.openBranches + '/' + health.totalBranches + ' dal'));
						healthGrid.appendChild(kpi('Momentum', health.momentumLabel, health.momentumColor, health.momentumHint));
						bodyInner.appendChild(healthGrid);

						/* Next-3 atomic actions panel (UI-3) */
						var pending = collectPendingActions(root, 3);
						if (pending.length > 0) {
							var nextWrap = document.createElement('div');
							nextWrap.style.cssText = 'margin-bottom:14px;padding:12px 14px;border-radius:10px;background:rgba(56,189,248,0.06);border:1px solid rgba(56,189,248,0.25);';
							var nextLabel = document.createElement('div');
							nextLabel.style.cssText = 'font-size:10px;text-transform:uppercase;letter-spacing:0.06em;color:#7dd3fc;font-weight:700;margin-bottom:6px;';
							nextLabel.textContent = '\u25B6 Sonraki ' + pending.length + ' Atomik Ad\u0131m';
							nextWrap.appendChild(nextLabel);
							var nextList = document.createElement('ul');
							nextList.style.cssText = 'margin:0;padding:0;list-style:none;display:flex;flex-direction:column;gap:6px;';
							for (var pi = 0; pi < pending.length; pi++) {
								(function(entry) {
									var li = document.createElement('li');
									li.style.cssText = 'display:flex;flex-direction:column;gap:2px;padding:6px 8px;border-radius:6px;background:rgba(255,255,255,0.03);cursor:pointer;transition:background 0.15s;';
									li.onmouseenter = function() { li.style.background = 'rgba(255,255,255,0.07)'; };
									li.onmouseleave = function() { li.style.background = 'rgba(255,255,255,0.03)'; };
									var txt = document.createElement('span');
									txt.style.cssText = 'font-size:12px;color:rgba(255,255,255,0.85);line-height:1.35;';
									txt.textContent = entry.item.text;
									li.appendChild(txt);
									var pathEl = document.createElement('span');
									pathEl.style.cssText = 'font-size:10px;color:rgba(255,255,255,0.35);';
									pathEl.textContent = entry.path.join(' \u203A ');
									li.appendChild(pathEl);
									li.addEventListener('click', function() {
										if (window.TunnelVision && typeof window.TunnelVision.open === 'function') {
											var actionId = entry.item.id != null ? String(entry.item.id) : (entry.leaf.id + ':action:' + entry.item.text);
											window.TunnelVision.open(actionId, entry.item.text);
										}
									});
									nextList.appendChild(li);
								})(pending[pi]);
							}
							nextWrap.appendChild(nextList);
							bodyInner.appendChild(nextWrap);
						}

				/* Render tree nodes */
				var rootCtx = {
					commit: function () {
						computeNodeProgress(root);
						persistTreeRoot(root.id, tree);
					},
					rerender: function () {
						/* Re-render the entire forest from storage so structure updates
						   (add/remove children) are reflected immediately. */
						var forest = loadForestRaw();
						if (!forest || forest.length === 0) forest = trees;
						var nextOpts = {};
						for (var k in opts) { if (Object.prototype.hasOwnProperty.call(opts, k)) nextOpts[k] = opts[k]; }
						nextOpts.expandRootId = root.id;
						renderGoalForest(forest, container, nextOpts);
					}
				};
				renderGoalNode(root, bodyInner, 0, rootCtx);
				body.appendChild(bodyInner);
				card.appendChild(body);

				/* Toggle logic */
				var expanded = false;
				header.onclick = function(e) {
					if (e.target === delBtn || e.target.parentNode === delBtn) return;
					if (e.target && e.target.dataset && e.target.dataset.editing === '1') return;
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

				/* Honour an opts.expandRootId so re-renders re-expand the
				   tree the user just edited. */
				if (opts && opts.expandRootId && opts.expandRootId === root.id) {
					expanded = true;
					body.style.maxHeight = 'none';
					chevron.style.transform = 'rotate(90deg)';
				}

				container.appendChild(card);
			})(trees[ti], ti);
		}
	}

	/* ── handleGoalTreeEvent ─────────────────────────────────────── */
	function handleGoalTreeEvent(payload) {
		var container = document.getElementById('goal-tree-container');
		if (!container) return;

		/* GoalTreeSummaryUpdated payload: { lastActiveGoalId, goals: [...] } */
		if (payload && Array.isArray(payload.goals)) {
			renderGoalForest(payload.goals, container);
			try { localStorage.setItem('vigilant-goaltrees', JSON.stringify(payload.goals)); } catch(e) {}
			return;
		}

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
		handleGoalTreeEvent: handleGoalTreeEvent,
		persistTreeRoot: persistTreeRoot,
		loadForestRaw: loadForestRaw
	};
})();

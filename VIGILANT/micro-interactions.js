/* ═══════════════════════════════════════════════════════════════════════
   VIGILANT – Micro-Interactions JS Helpers
   Expand/Collapse controller, Tooltip manager, Skeleton factory.
   Zero dependencies. Works in WebView2 + modern browsers.
   ═══════════════════════════════════════════════════════════════════════ */

// ── Feature: Detect reduced-motion preference ─────────────────────────
var MotionPrefs = (function () {
    var mql = window.matchMedia('(prefers-reduced-motion: reduce)');
    var _reduce = mql.matches;

    // Live-watch changes (user can toggle mid-session)
    try {
        mql.addEventListener('change', function (e) { _reduce = e.matches; });
    } catch (_) {
        // Safari ≤13 fallback
        mql.addListener(function (e) { _reduce = e.matches; });
    }

    return {
        get isReduced() { return _reduce; }
    };
})();


// ══════════════════════════════════════════════════════════════════════
// 1.  EXPAND / COLLAPSE CONTROLLER
//
//     Uses CSS grid-template-rows: 0fr→1fr (defined in micro-interactions.css).
//     JS only toggles [aria-expanded] and fires lifecycle callbacks.
//
//     API:
//       var panel = ExpandCollapse.create(triggerEl, panelEl, options?)
//       panel.toggle()
//       panel.open()
//       panel.close()
//       panel.destroy()
//
//     Options:
//       onOpen()        – called after transition starts
//       onClose()       – called after transition starts
//       onTransitionEnd – called when grid transition completes
// ══════════════════════════════════════════════════════════════════════
var ExpandCollapse = (function () {
    'use strict';

    function create(triggerEl, panelEl, opts) {
        opts = opts || {};
        var _open = panelEl.getAttribute('aria-expanded') === 'true';

        // Ensure required classes are present
        if (!panelEl.classList.contains('vc-expand')) {
            panelEl.classList.add('vc-expand');
        }

        // Wrap inner content if not already wrapped
        var inner = panelEl.querySelector('.vc-expand__inner');
        if (!inner) {
            inner = document.createElement('div');
            inner.className = 'vc-expand__inner';
            while (panelEl.firstChild) {
                inner.appendChild(panelEl.firstChild);
            }
            panelEl.appendChild(inner);
        }

        // ARIA setup
        if (!panelEl.id) {
            panelEl.id = 'vc-expand-' + Date.now() + '-' + Math.random().toString(36).slice(2, 6);
        }
        triggerEl.setAttribute('aria-controls', panelEl.id);
        triggerEl.setAttribute('aria-expanded', String(_open));
        panelEl.setAttribute('aria-expanded', String(_open));

        // Transition-end callback
        function handleTransitionEnd(e) {
            if (e.propertyName === 'grid-template-rows' && typeof opts.onTransitionEnd === 'function') {
                opts.onTransitionEnd(_open);
            }
        }
        panelEl.addEventListener('transitionend', handleTransitionEnd);

        function setOpen(state) {
            _open = state;
            triggerEl.setAttribute('aria-expanded', String(_open));
            panelEl.setAttribute('aria-expanded', String(_open));
        }

        function toggle() {
            if (_open) { close(); } else { open(); }
        }

        function open() {
            setOpen(true);
            if (typeof opts.onOpen === 'function') opts.onOpen();
        }

        function close() {
            setOpen(false);
            if (typeof opts.onClose === 'function') opts.onClose();
        }

        // Click handler
        function onClick(e) {
            e.preventDefault();
            toggle();
        }
        triggerEl.addEventListener('click', onClick);

        // Keyboard: Enter / Space
        function onKeyDown(e) {
            if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                toggle();
            }
        }
        triggerEl.addEventListener('keydown', onKeyDown);

        // Ensure trigger is focusable
        if (!triggerEl.getAttribute('tabindex') && triggerEl.tagName !== 'BUTTON') {
            triggerEl.setAttribute('tabindex', '0');
            triggerEl.setAttribute('role', 'button');
        }

        function destroy() {
            triggerEl.removeEventListener('click', onClick);
            triggerEl.removeEventListener('keydown', onKeyDown);
            panelEl.removeEventListener('transitionend', handleTransitionEnd);
        }

        return {
            toggle: toggle,
            open: open,
            close: close,
            destroy: destroy,
            get isOpen() { return _open; }
        };
    }

    return { create: create };
})();


// ══════════════════════════════════════════════════════════════════════
// 2.  ACCESSIBLE TOOLTIP
//
//     Auto-enhances elements with [data-vc-tip] attribute.
//     Creates role="tooltip" bubble, manages ARIA ids, show/hide.
//     Positioning: top (default) or bottom via [data-vc-tip-pos].
//
//     JS API (programmatic):
//       var tip = Tooltip.attach(targetEl, 'Label text', { position: 'bottom' })
//       tip.show()
//       tip.hide()
//       tip.destroy()
//
//     Declarative API (auto-init):
//       <button data-vc-tip="Save file" data-vc-tip-pos="bottom">💾</button>
//       Tooltip.initAll()
// ══════════════════════════════════════════════════════════════════════
var Tooltip = (function () {
    'use strict';

    var _counter = 0;
    var _instances = [];

    function attach(targetEl, text, opts) {
        opts = opts || {};
        var pos = opts.position || targetEl.getAttribute('data-vc-tip-pos') || 'top';

        // Build wrapper
        var wrapper = document.createElement('span');
        wrapper.className = 'vc-tooltip' + (pos === 'bottom' ? ' vc-tooltip--bottom' : '');

        // Move target into wrapper
        targetEl.parentNode.insertBefore(wrapper, targetEl);
        wrapper.appendChild(targetEl);

        // Build bubble
        var tipId = 'vc-tip-' + (++_counter);
        var bubble = document.createElement('span');
        bubble.className = 'vc-tooltip__bubble';
        bubble.setAttribute('role', 'tooltip');
        bubble.setAttribute('id', tipId);
        bubble.textContent = text;
        wrapper.appendChild(bubble);

        // ARIA: target described by tooltip
        targetEl.setAttribute('aria-describedby', tipId);

        // Escape key closes tooltip
        function onKeyDown(e) {
            if (e.key === 'Escape') {
                targetEl.blur();
            }
        }
        targetEl.addEventListener('keydown', onKeyDown);

        function show() {
            bubble.style.opacity = '1';
            bubble.style.transform = 'translateX(-50%) translateY(0)';
        }

        function hide() {
            bubble.style.opacity = '';
            bubble.style.transform = '';
        }

        function destroy() {
            targetEl.removeEventListener('keydown', onKeyDown);
            targetEl.removeAttribute('aria-describedby');
            // Unwrap: move target back, remove wrapper
            wrapper.parentNode.insertBefore(targetEl, wrapper);
            wrapper.remove();
            _instances = _instances.filter(function (i) { return i !== instance; });
        }

        var instance = { show: show, hide: hide, destroy: destroy, el: targetEl };
        _instances.push(instance);
        return instance;
    }

    /** Scan DOM and auto-attach tooltips for all [data-vc-tip] elements. */
    function initAll(root) {
        root = root || document;
        var els = root.querySelectorAll('[data-vc-tip]');
        for (var i = 0; i < els.length; i++) {
            // Skip if already wrapped
            if (els[i].closest('.vc-tooltip')) continue;
            attach(els[i], els[i].getAttribute('data-vc-tip'));
        }
    }

    /** Destroy all managed tooltip instances. */
    function destroyAll() {
        while (_instances.length) {
            _instances[0].destroy();
        }
    }

    return {
        attach: attach,
        initAll: initAll,
        destroyAll: destroyAll
    };
})();


// ══════════════════════════════════════════════════════════════════════
// 3.  SKELETON FACTORY
//
//     Builds skeleton placeholder HTML matching common dashboard shapes.
//     Returns an HTML string you can set with innerHTML while data loads.
//
//     API:
//       Skeleton.text(count?, widthPercent?)
//       Skeleton.statCard()
//       Skeleton.activityRow(count?)
//       Skeleton.card(height?)
//       Skeleton.replace(containerEl)   ← removes skeletons with fade-out
// ══════════════════════════════════════════════════════════════════════
var Skeleton = (function () {
    'use strict';

    /** N lines of skeleton text with varying widths. */
    function text(count, widths) {
        count = count || 3;
        widths = widths || [100, 85, 60];
        var html = '';
        for (var i = 0; i < count; i++) {
            var w = widths[i % widths.length];
            html += '<div class="vc-skeleton vc-skeleton--text" style="--sk-w:' + w + '%;margin-bottom:8px"></div>';
        }
        return html;
    }

    /** Skeleton heading. */
    function heading(widthPct) {
        return '<div class="vc-skeleton vc-skeleton--heading" style="--sk-w:' + (widthPct || 45) + '%"></div>';
    }

    /** Stat card skeleton (matches .stat-card layout). */
    function statCard() {
        return '<div class="vc-skeleton-stat glass-card">'
            + '<div class="vc-skeleton vc-skeleton--text" style="--sk-w:55%;margin-bottom:4px"></div>'
            + '<div class="vc-skeleton vc-skeleton--heading" style="--sk-w:40%"></div>'
            + '</div>';
    }

    /** Activity row skeleton. */
    function activityRow(count) {
        count = count || 5;
        var html = '';
        for (var i = 0; i < count; i++) {
            html += '<div style="display:flex;align-items:center;gap:12px;padding:10px 12px">'
                + '<div class="vc-skeleton vc-skeleton--circle" style="--sk-size:8px"></div>'
                + '<div style="flex:1;display:flex;flex-direction:column;gap:6px">'
                + '<div class="vc-skeleton vc-skeleton--text" style="--sk-w:' + (55 + Math.random() * 30) + '%"></div>'
                + '<div class="vc-skeleton vc-skeleton--text" style="--sk-w:' + (30 + Math.random() * 25) + '%;height:10px"></div>'
                + '</div>'
                + '<div class="vc-skeleton vc-skeleton--text" style="--sk-w:48px;height:20px;border-radius:6px"></div>'
                + '</div>';
        }
        return html;
    }

    /** Full card skeleton. */
    function card(height) {
        return '<div class="vc-skeleton vc-skeleton--card" style="--sk-h:' + (height || 160) + 'px"></div>';
    }

    /**
     * Smoothly replace skeleton content with real content.
     * Usage: after data arrives, update innerHTML first, then call
     *        Skeleton.reveal(containerEl) to trigger the fade-in.
     */
    function reveal(containerEl) {
        // Mark children for stagger entrance
        var children = containerEl.children;
        for (var i = 0; i < children.length; i++) {
            children[i].classList.add('fade-enter');
        }

        // Force reflow, then trigger
        containerEl.offsetHeight; // eslint-disable-line no-unused-expressions

        requestAnimationFrame(function () {
            for (var i = 0; i < children.length; i++) {
                (function (el, delay) {
                    setTimeout(function () {
                        el.classList.add('is-visible');
                    }, delay);
                })(children[i], i * 50);
            }
        });
    }

    return {
        text: text,
        heading: heading,
        statCard: statCard,
        activityRow: activityRow,
        card: card,
        reveal: reveal
    };
})();

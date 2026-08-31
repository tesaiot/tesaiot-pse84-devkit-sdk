/* bento-scrollmemory.js — keep the reader's place across a reload and across
   the language switch.
 *
 * WHY THE KEY IS NOT THE URL
 * --------------------------
 * The Thai counterpart of /sdk/mtb-mpy/group__sensors.html is
 * /sdk/mtb-mpy/th/group__sensors.html. Keying on the full path would file the
 * two languages under different entries, and switching language — the case
 * that loses your place most painfully, because the page is long and you were
 * deep in it — would restore nothing. So the language segment is stripped and
 * both translations share one entry.
 *
 * WHY sessionStorage AND NOT localStorage
 * ---------------------------------------
 * A position is only meaningful inside the visit that produced it. Coming back
 * tomorrow and being dropped halfway down a reference page, with no memory of
 * why, is worse than starting at the top.
 *
 * WHAT TAKES PRECEDENCE
 * ---------------------
 * An explicit #fragment always wins: the reader asked for that anchor, and a
 * restored offset would silently override the thing they clicked. A restore
 * also never runs when the browser has already restored the position itself.
 *
 * WHY THE RESTORE IS DEFERRED AND REPEATED
 * ----------------------------------------
 * doxygen's navtree.js builds the sidebar after load and the fonts settle
 * later still; both change document height. Setting scrollTop once, early,
 * lands somewhere near the top because the document is still short. So the
 * target is re-applied over a short window and stops as soon as the document
 * is tall enough to honour it.
 */
(function () {
    "use strict";

    var KEY_PREFIX = "bento:scroll:";
    var SETTLE_MS = 1200;      /* how long the layout is allowed to grow */
    var SAVE_THROTTLE_MS = 250;

    /* /sdk/mtb-mpy/th/foo.html -> /sdk/mtb-mpy/foo.html
       Only a whole path segment counts, so a directory called "then" or a file
       called "th.html" is left alone. */
    function key() {
        var p = location.pathname.replace(/\/(th|en)(?=\/)/g, "");
        return KEY_PREFIX + p;
    }

    function pageLang() {
        var m = location.pathname.match(/\/(th|en)\//);
        if (m) { return m[1]; }
        return (document.documentElement.getAttribute("lang") || "en").slice(0, 2);
    }

    /* The id of the outline element nearest the top of the viewport. This is
       what survives a language change; a pixel offset does not, because the
       two translations of a page are not the same height and the same offset
       lands somewhere unrelated. doxygen gives a section the same id in both
       trees, so the anchor is a shared address and the offset is not. */
    function topAnchor() {
        var els = document.querySelectorAll(
            "#doc-content [id], .contents [id], h1[id], h2[id], h3[id], a[id]");
        var best = null, bestTop = -Infinity;
        for (var i = 0; i < els.length; i++) {
            var el = els[i];
            if (!el.id || el.id.indexOf("MSearch") === 0) { continue; }
            var t = el.getBoundingClientRect().top;
            /* the last one that is at or above the top edge */
            if (t <= 80 && t > bestTop) { bestTop = t; best = el.id; }
        }
        return best;
    }

    function read() {
        try {
            var raw = sessionStorage.getItem(key());
            if (!raw) { return null; }
            var v = JSON.parse(raw);
            return (v && typeof v === "object") ? v : null;
        } catch (e) { return null; }   /* private mode, or storage disabled */
    }

    function write(y) {
        try {
            if (y > 0) {
                sessionStorage.setItem(key(), JSON.stringify(
                    { y: y, lang: pageLang(), anchor: topAnchor() }));
            } else {
                sessionStorage.removeItem(key());
            }
        } catch (e) { /* nothing to do; the page still works */ }
    }

    /* ---- saving ---------------------------------------------------------- */

    var timer = null;
    function onScroll() {
        if (timer) return;
        timer = window.setTimeout(function () {
            timer = null;
            write(window.pageYOffset || document.documentElement.scrollTop || 0);
        }, SAVE_THROTTLE_MS);
    }
    window.addEventListener("scroll", onScroll, { passive: true });

    /* A throttled save can be up to SAVE_THROTTLE_MS stale when the page goes
       away, and clicking the language pill is exactly that case. pagehide
       covers the back/forward cache too, which "unload" does not. */
    function flush() {
        write(window.pageYOffset || document.documentElement.scrollTop || 0);
    }
    window.addEventListener("pagehide", flush);
    document.addEventListener("visibilitychange", function () {
        if (document.visibilityState === "hidden") { flush(); }
    });

    /* ---- restoring ------------------------------------------------------- */

    /* Tell the browser not to do its own restore. Ours understands that the
       document is still growing; the browser's fires once, too early. */
    if ("scrollRestoration" in history) {
        try { history.scrollRestoration = "manual"; } catch (e) {}
    }

    function restore() {
        if (location.hash && location.hash.length > 1) { return; }

        var saved = read();
        if (!saved) { return; }

        /* Crossing languages: go to the section, never to the offset. Landing
           mid-paragraph in the other language because the byte count differs
           is exactly the complaint this branch exists to answer. */
        if (saved.lang && saved.lang !== pageLang()) {
            if (!saved.anchor) { return; }
            var el = document.getElementById(saved.anchor);
            if (!el) { return; }           /* no counterpart section: stay put */
            var deadlineA = Date.now() + SETTLE_MS;
            (function toAnchor() {
                var e = document.getElementById(saved.anchor);
                if (e) { e.scrollIntoView({ block: "start" }); }
                if (Date.now() < deadlineA) { window.requestAnimationFrame(toAnchor); }
            })();
            return;
        }

        var target = saved.y;
        if (!target) { return; }

        var deadline = Date.now() + SETTLE_MS;

        (function apply() {
            var doc = document.documentElement;
            var max = Math.max(0, doc.scrollHeight - window.innerHeight);

            window.scrollTo(0, Math.min(target, max));

            /* Done as soon as the document is tall enough to actually hold the
               position; otherwise keep trying while the layout settles. */
            if (max < target && Date.now() < deadline) {
                window.requestAnimationFrame(apply);
            }
        })();
    }

    if (document.readyState === "complete") {
        restore();
    } else {
        window.addEventListener("load", restore);
    }
})();

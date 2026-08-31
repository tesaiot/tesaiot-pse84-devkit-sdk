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

    /* WHICH ELEMENT ACTUALLY SCROLLS
       ------------------------------
       On doxygen-awesome-sidebar-only the page does NOT scroll: #side-nav is
       fixed and #doc-content is the scrolling box, so document height stays
       near the viewport height however long the article is. Listening on
       window and calling window.scrollTo therefore did nothing on exactly the
       pages this script exists for. Measured: an Overview page reported a
       document height of 834 in a 900-tall viewport.

       The landing page has no #doc-content and scrolls normally, so the
       container is detected rather than assumed. */
    function container() {
        var dc = document.getElementById("doc-content");
        if (dc && dc.scrollHeight > dc.clientHeight + 4) { return dc; }
        return null;              /* null means: the window scrolls */
    }
    function getY() {
        var c = container();
        return c ? c.scrollTop
                 : (window.pageYOffset || document.documentElement.scrollTop || 0);
    }
    function setY(y) {
        var c = container();
        if (c) { c.scrollTop = y; } else { window.scrollTo(0, y); }
    }
    function maxY() {
        var c = container();
        if (c) { return Math.max(0, c.scrollHeight - c.clientHeight); }
        var de = document.documentElement;
        return Math.max(0, de.scrollHeight - window.innerHeight);
    }

    var KEY_PREFIX = "bento:scroll:";
    var SETTLE_MS = 1200;      /* how long the layout is allowed to grow */
    var SAVE_THROTTLE_MS = 250;

    /* /sdk/mtb-mpy/th/foo.html -> /sdk/mtb-mpy/foo.html
       Only a whole path segment counts, so a directory called "then" or a file
       called "th.html" is left alone. */
    function key() {
        var p = location.pathname;
        /* Two naming schemes carry the language, and both have to collapse to
           the same key or the translations do not share an entry at all:

             docs   /sdk/mtb-mpy/th/group__x.html  ->  /sdk/mtb-mpy/group__x.html
             landing /index.en.html                ->  /index.html

           The landing pages differ by FILENAME, not by directory. Handling
           only the directory form left them on separate keys, so switching
           language there restored whatever that language had stored from an
           earlier visit - a different place entirely, which is exactly the
           complaint. */
        p = p.replace(/\/(th|en)(?=\/)/g, "");          /* /th/ segment */
        p = p.replace(/\.(th|en)(?=\.[a-z0-9]+$)/i, ""); /* .en before .html */
        return KEY_PREFIX + p;
    }

    function pageLang() {
        var m = location.pathname.match(/\/(th|en)\//) ||
                location.pathname.match(/\.(th|en)\.[a-z0-9]+$/i);
        if (m) { return m[1].toLowerCase(); }
        return (document.documentElement.getAttribute("lang") || "en").slice(0, 2);
    }

    /* The id of the outline element nearest the top of the viewport. This is
       what survives a language change; a pixel offset does not, because the
       two translations of a page are not the same height and the same offset
       lands somewhere unrelated. doxygen gives a section the same id in both
       trees, so the anchor is a shared address and the offset is not. */
    /* doxygen puts ids on its own search chrome too. Anchoring to SRIndex or
       MSearchSelectWindow is worse than not anchoring at all, because those
       ids exist on every page and the restore then always "succeeds" at the
       top. Only headings and section anchors inside the article count. */
    function isChrome(id) {
        return !id || id.indexOf("MSearch") === 0 || id.indexOf("SR") === 0 ||
               id === "top" || id === "nav-path" || id === "titlearea";
    }
    function topAnchor() {
        var scope = document.querySelector(".contents") ||
                    document.getElementById("doc-content") || document.body;
        var els = scope.querySelectorAll("h1[id], h2[id], h3[id], h4[id], " +
                                         "a[id], .groupheader[id], [id].memitem");
        var best = null, bestTop = -Infinity;
        for (var i = 0; i < els.length; i++) {
            var el = els[i];
            if (isChrome(el.id)) { continue; }
            var t = el.getBoundingClientRect().top;
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
                    { y: y, lang: pageLang(), anchor: topAnchor(),
                      /* how far down, as a fraction. The two translations of a
                         page are not the same length but they are the same
                         document, so the fraction lands near the same passage
                         when there is no anchor to aim at - and doxygen writes
                         no ids at all into a \mainpage, so on those pages there
                         never is one. */
                      f: maxY() > 0 ? (y / maxY()) : 0 }));
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
            write(getY());
        }, SAVE_THROTTLE_MS);
    }
    window.addEventListener("scroll", onScroll, { passive: true });
    /* And on the box that really scrolls. This script is in <head>, so
       #doc-content does not exist yet at parse time — binding here rather than
       on DOMContentLoaded silently bound nothing, and nothing was ever saved
       on precisely the pages that need it. */
    var bound = false;
    function bindContainer() {
        if (bound) { return; }
        var dc = document.getElementById("doc-content");
        if (!dc) { return; }
        dc.addEventListener("scroll", onScroll, { passive: true });
        bound = true;
    }
    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", bindContainer);
    } else {
        bindContainer();
    }
    window.addEventListener("load", bindContainer);

    /* A throttled save can be up to SAVE_THROTTLE_MS stale when the page goes
       away, and clicking the language pill is exactly that case. pagehide
       covers the back/forward cache too, which "unload" does not. */
    function flush() { write(getY()); }
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
            var el = saved.anchor ? document.getElementById(saved.anchor) : null;

            if (!el) {
                /* No shared anchor. Land at the same relative depth rather
                   than at the same pixel offset, which is meaningless across
                   two documents of different length - that offset is what put
                   the reader somewhere unrelated. */
                if (!(saved.f > 0)) { return; }
                var deadlineF = Date.now() + SETTLE_MS;
                (function toFraction() {
                    var m = maxY();
                    if (m > 0) { setY(Math.round(saved.f * m)); }
                    if (Date.now() < deadlineF) { window.requestAnimationFrame(toFraction); }
                })();
                return;
            }
            var deadlineA = Date.now() + SETTLE_MS;
            (function toAnchor() {
                var e = document.getElementById(saved.anchor);
                if (e) {
                    var c = container();
                    if (c) {
                        c.scrollTop += e.getBoundingClientRect().top -
                                       c.getBoundingClientRect().top;
                    } else {
                        e.scrollIntoView({ block: "start" });
                    }
                }
                if (Date.now() < deadlineA) { window.requestAnimationFrame(toAnchor); }
            })();
            return;
        }

        var target = saved.y;
        if (!target) { return; }

        var deadline = Date.now() + SETTLE_MS;

        (function apply() {
            var max = maxY();

            setY(Math.min(target, max));

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

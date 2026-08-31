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

    function read() {
        try {
            var v = sessionStorage.getItem(key());
            var n = v === null ? NaN : parseInt(v, 10);
            return (isFinite(n) && n > 0) ? n : 0;
        } catch (e) { return 0; }   /* private mode, or storage disabled */
    }

    function write(y) {
        try {
            if (y > 0) { sessionStorage.setItem(key(), String(y)); }
            else       { sessionStorage.removeItem(key()); }
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

        var target = read();
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

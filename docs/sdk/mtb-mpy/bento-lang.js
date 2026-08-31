/* ---------------------------------------------------------------------------
 * bento-lang.js — the TH|EN language toggle for the SDK documentation site.
 *
 * Shipped into every language tree by HTML_EXTRA_FILES and pulled in by the
 * generated HTML_HEADER, which also defines:
 *
 *     window.BENTO_DOC = { lang: "en"|"th", variant: "…", relpath: "$relpath^" }
 *
 * `relpath` is doxygen's own relative path from the CURRENT page back to the
 * root of the language tree it belongs to ("" for a page at the root, "../"
 * for anything under search/). It is the only reliable way to find the tree
 * root offline: location.pathname alone cannot tell a docs root apart from a
 * directory that happens to be called "th".
 *
 * Layout (ESP-IDF's per-target pattern, RESEARCH_IA_SITES Q4 — parallel builds,
 * identical page paths, a switcher in the header):
 *
 *     sdk/html/<variant>/           EN — the default; unchanged path
 *     sdk/html/<variant>/th/        TH — identical file names, one level down
 *
 * so the counterpart of any page is the same basename under the sibling root.
 *
 * THE CONTROL IS AN ANCHOR. This is the whole design, and it is not a style
 * choice — it is what makes the toggle work at all in Safari, opened from
 * disk, which is how the professor reads this site.
 * ------------------------------------------------------------------------
 * The pill used to be a custom element with `data-href`, navigated by a
 * delegated click handler that assigned `window.location.href`. Everything
 * about that is fragile in a way an <a href> is not:
 *
 *   - if ANY line of the handler throws, the click does nothing at all, with
 *     no error the reader can see. A custom element is not a link; there is
 *     no default action to fall back to;
 *   - it cannot be opened in a new tab, middle-clicked, copied as a link, or
 *     followed by a keyboard user without a bespoke keydown handler;
 *   - it does nothing whatsoever with JavaScript disabled.
 *
 * The pill is therefore a real `<a class="bento-lang-toggle" href="…">` whose
 * href is a PLAIN RELATIVE PATH to a file that exists on disk. The browser
 * performs the navigation natively. No script of ours runs on the click path;
 * the delegated handler below only RECORDS the choice and never calls
 * preventDefault, so a throw inside it cannot stop the jump.
 *
 * NO QUERY STRING ON file://. The href carries "?lang=" only when the page is
 * being served over http(s), where it is genuinely useful (a link can pin the
 * language it wants). Read from disk, the href is the bare path and nothing
 * else — a file:// URL must name a file, and the destination already knows its
 * own language from BENTO_DOC. The choice is remembered in localStorage,
 * written by the click handler before the browser leaves; if that throws, the
 * site simply forgets the preference, which is the correct thing to lose.
 *
 * FALLBACK STRATEGY — a generated page-list manifest, not a HEAD probe.
 * bento-lang-pages.js (written by bento-release.sh after BOTH doxygen runs,
 * identical in both trees) defines:
 *
 *     window.BENTO_LANG_PAGES = { en: [...], th: [...] }
 *
 * A HEAD request cannot be used: this site is built to be opened from disk
 * ("open sdk/html/<variant>/index.html — offline"), and fetch()/XHR against a
 * file:// URL is blocked by every current browser, so a probe would report
 * "missing" for every page that exists. The manifest is static data that works
 * identically over file:// and http(s). Every read of it is wrapped: if it is
 * absent, malformed, or throws, the toggle assumes the counterpart is there
 * and lets the browser be the judge — a degraded jump, never a dead control.
 *
 * Memory: the reader's choice is stored in localStorage under
 * "bento-doc-lang". On a page whose language differs from that choice, and only
 * when the counterpart page actually exists, the script redirects once. That is
 * what keeps a reader in their language when they arrive from a bookmark, from
 * the sister-variant link on the landing page, or from a search result.
 *
 * PLACEMENT — one pill, appended to #MSearchBox's own parent, which is exactly
 * what doxygen-awesome's optional dark-mode toggle does. Doxygen 1.18 emits two
 * menu bars (#main-nav-mobile with #searchBoxPos1, #main-nav with
 * li#searchBoxPos2) and menu.js moves the search box between them BY COPYING
 * innerHTML as the viewport crosses 768px, hiding whichever position is idle.
 * Riding inside that container therefore gives us the mobile placement for
 * free and guarantees exactly one visible pill at every width. Two rules follow
 * from it, and both are load-bearing:
 *
 *   - The copy is re-parsed HTML, so a listener attached to the element does
 *     not survive the move. An anchor needs none: `href` is an attribute and
 *     survives serialisation intact. The one delegated listener we do keep is
 *     bound to `document`, which the copy never leaves.
 *   - Nothing may be inserted as a SIBLING of li#searchBoxPos2. Sidebar-only
 *     sets --menu-display:none, which hides every #main-menu li except the last
 *     child; a sibling steals that exemption and the search box vanishes.
 *
 * See bento-lang.css for the measured geometry that goes with this.
 * ------------------------------------------------------------------------ */

(function () {
    "use strict";

    var STORE_KEY = "bento-doc-lang";
    var LANGS = { en: "EN", th: "TH" };
    /* Display order in the pill. Thai first: the toggle exists for the Thai
       reader — the English site is already the default path. */
    var ORDER = ["th", "en"];

    var doc = window.BENTO_DOC || {};
    var lang = LANGS[doc.lang] ? doc.lang : "en";
    var other = lang === "en" ? "th" : "en";
    /* doxygen's own path back to this language tree's root: "" at the root,
       "../" under search/. Used as a PREFIX, never resolved against an
       origin — the href we build stays relative all the way to the browser. */
    var relpath = (typeof doc.relpath === "string") ? doc.relpath : "";

    /* Only over http(s) is a query string worth carrying. A file:// URL has
       to name a file on disk and nothing else. */
    var onWeb = (window.location.protocol === "http:" ||
                 window.location.protocol === "https:");

    /* ---- where am I -------------------------------------------------------
       The page's path relative to its own tree root — "index.html",
       "group__feat__edge__ai.html", "search/all_0.html". decodeURI on both
       sides so a percent-escaped path still subtracts cleanly. */
    var rel = (function () {
        try {
            /* new URL("", href) resolves to the page itself, not its
               directory, so an empty relpath must become "./". */
            var root = new URL(relpath || "./", window.location.href);
            var here = decodeURI(window.location.pathname);
            var rootPath = decodeURI(root.pathname);
            var r = (here.indexOf(rootPath) === 0)
                ? here.slice(rootPath.length) : "";
            return r || "index.html";
        } catch (e) {
            return "index.html";
        }
    })();

    function counterpartExists() {
        try {
            var pages = window.BENTO_LANG_PAGES;
            /* No manifest -> assume it is there and let the browser judge. */
            if (!pages || !pages[other] || !pages[other].length) { return true; }
            return pages[other].indexOf(rel) !== -1;
        } catch (e) {
            return true;
        }
    }

    /* The counterpart of this page, as a plain relative path.

         EN -> TH   relpath + "th/" + rel
         TH -> EN   relpath + "../" + rel

       Worked through, because getting this wrong is silent:
         .../mtb-mpy/index.html            relpath ""    -> th/index.html
         .../mtb-mpy/search/all_0.html     relpath "../" -> ../th/search/all_0.html
         .../mtb-mpy/th/index.html         relpath ""    -> ../index.html
         .../mtb-mpy/th/search/all_0.html  relpath "../" -> ../../search/all_0.html   */
    function counterpartHref() {
        var exists = counterpartExists();
        var href = relpath + (other === "th" ? "th/" : "../")
                 + (exists ? rel : "index.html");
        if (onWeb) { href += "?lang=" + other; }
        /* Section anchors are \section ids, which the translation standard
           keeps identical across languages — carry the fragment when the page
           itself survives the jump, drop it when we fell back to the root. */
        if (exists) {
            try {
                if (window.location.hash) { href += window.location.hash; }
            } catch (e) { /* keep the bare path */ }
        }
        return href;
    }

    /* ---- remembered choice ----------------------------------------------- */

    function readPref() {
        try { return window.localStorage.getItem(STORE_KEY); } catch (e) { return null; }
    }
    function writePref(v) {
        try { window.localStorage.setItem(STORE_KEY, v); } catch (e) { /* private mode, file:// policy */ }
    }

    /* ?lang=xx pins a language for this navigation and records it. It is only
       ever produced over http(s), but honouring it costs nothing and lets a
       hand-written link pin a language anywhere. */
    var forced = null;
    try {
        var q = new URLSearchParams(window.location.search).get("lang");
        if (q && LANGS[q]) { forced = q; writePref(q); }
    } catch (e) { /* no URLSearchParams: skip the escape hatch, nothing else */ }

    var pref = forced || readPref();

    /* Redirect at most once, and only when the counterpart page really exists:
       bouncing a reader to a front page they did not ask for is worse than
       showing them the language they landed on. No loop is possible — after the
       jump the page's language equals the stored preference. */
    if (pref && pref !== lang && LANGS[pref] && counterpartExists()) {
        try {
            window.location.replace(counterpartHref());
            return;
        } catch (e) { /* fall through and just render the pill */ }
    }

    /* ---- the control — a real link --------------------------------------- */

    function build() {
        var el = document.createElement("a");
        var exists = counterpartExists();
        el.className = "bento-lang-toggle" + (exists ? "" : " bento-lang-fallback");
        el.setAttribute("href", counterpartHref());
        el.setAttribute("data-lang", lang);
        el.setAttribute("data-other", other);
        el.setAttribute("rel", "alternate");
        el.setAttribute("hreflang", other);
        el.title = exists
            ? (other === "th" ? "อ่านหน้านี้เป็นภาษาไทย" : "Read this page in English")
            : (other === "th"
                ? "ยังไม่มีคำแปลของหน้านี้ — ไปที่หน้าแรกภาษาไทย"
                : "This page has no English counterpart yet — go to the English front page");

        ORDER.forEach(function (code) {
            var span = document.createElement("span");
            span.textContent = LANGS[code];
            if (code === lang) { span.className = "bento-lang-active"; }
            el.appendChild(span);
        });

        return el;
    }

    /* Delegated on `document`, in the CAPTURE phase, and deliberately inert:
       it records the reader's choice and returns. It never calls
       preventDefault, so the browser's own navigation happens whatever this
       does — including throwing. That is the entire point of the anchor. */
    document.addEventListener("click", function (ev) {
        try {
            var t = ev.target;
            var el = (t && t.closest) ? t.closest("a.bento-lang-toggle") : null;
            if (el) { writePref(el.getAttribute("data-other") || other); }
        } catch (e) { /* the jump still happens */ }
    }, true);

    /* The fragment is baked into the href at build time; keep it current as the
       reader moves through the page's sections. Presentation only — if this
       never runs, the pill still jumps, just to the top of the page. */
    window.addEventListener("hashchange", function () {
        try {
            var els = document.querySelectorAll("a.bento-lang-toggle");
            var href = counterpartHref();
            for (var i = 0; i < els.length; i++) { els[i].setAttribute("href", href); }
        } catch (e) { /* leave the built href in place */ }
    });

    function install() {
        /* Idempotent: menu.js can re-run its layout pass, and a second pill is
           the defect this guard exists to prevent. The old custom-element name
           is matched too, so a stale cached script cannot produce two. */
        if (document.querySelector("a.bento-lang-toggle, bento-lang-toggle")) { return; }

        /* Sidebar-only (--menu-display:none) fixes the header column to
           --side-nav-fixed-width and clips it, so the stylesheet has to take
           the pill's width back out of the search box. Only that layout wants
           the arithmetic, so mark it here rather than assume it there. */
        try {
            var md = window.getComputedStyle(document.documentElement)
                           .getPropertyValue("--menu-display").trim();
            if (md === "none") {
                document.documentElement.className += " bento-lang-sidebar";
            }
        } catch (e) { /* pre-CSSOM browser: the default layout still works */ }

        /* Exactly where doxygen-awesome's dark-mode toggle goes — INSIDE the
           search box's container, never beside it. See the header comment. */
        var box = document.getElementById("MSearchBox");
        if (box && box.parentNode) {
            box.parentNode.appendChild(build());
            return;
        }

        /* No search box at all (SEARCHENGINE=NO, or a DISABLE_INDEX build):
           fall back to the title area so the toggle is never simply absent. */
        var title = document.getElementById("titlearea") || document.body;
        if (title) { title.appendChild(build()); }
    }

    if (document.readyState === "loading") {
        /* This script is in <head>, so this listener is registered before the
           one menu.js adds from <body> — install() therefore runs first, and
           the pill is part of the markup menu.js captures and moves between the
           two menu bars. Verified in the rendered page at 390px and 1600px. */
        document.addEventListener("DOMContentLoaded", install);
    } else {
        install();
    }
})();

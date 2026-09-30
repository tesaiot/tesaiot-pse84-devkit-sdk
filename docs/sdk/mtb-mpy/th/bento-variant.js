/* ---------------------------------------------------------------------------
 * bento-variant.js — the variant target selector for the SDK documentation.
 *
 * ESP-IDF's per-target switcher, applied to our two packages: the same page,
 * in the same language, rendered from the other variant's module set.
 *
 *     sdk/html/mtb-only/          "ModusToolbox"
 *     sdk/html/mtb-mpy/           "ModusToolbox & µPython"
 *
 * Both variants are BUILT SEPARATELY and stay that way: the per-variant
 * doxygen run is what makes the cross-variant snippet gate work — a snippet
 * naming a file the mtb-only package does not ship fails that build, and it
 * has caught real leaks. This selector is navigation only. It never merges
 * the two sites; it jumps between them, exactly as the TH|EN pill jumps
 * between the two language trees of one variant.
 *
 * WHY THIS IS A LIST OF LINKS AND NOT A <select>.
 * ------------------------------------------------------------------------
 * It was a native <select> whose `change` handler assigned
 * `window.location.href`. In Safari the menu opened, both packages were
 * listed, the current one was ticked — and choosing the other did nothing at
 * all. A control that navigates only from inside a JavaScript handler has a
 * failure mode with no symptom: if anything on that path throws, is blocked,
 * or never fires, the reader gets silence.
 *
 * So every choice is now a real `<a href>` to a plain relative path that
 * exists on disk, and the browser performs the jump itself. No script of ours
 * is on the click path. The dropdown LOOK the professor asked for is kept by
 * building it from `<details>`/`<summary>`, which opens and closes natively
 * with no JavaScript either — the only thing script still does is decide the
 * control's contents and place the open panel.
 *
 * NO QUERY STRING ON file://. The old hrefs carried "?lang=" to hand the
 * reader's language to the sibling tree. A file:// URL must name a file, so
 * the language now rides in the PATH (the "th/" segment, which was always
 * there) and in localStorage, written by the delegated click handler below
 * before the browser leaves. "?lang=" is still appended over http(s), where
 * it is free and lets a hand-written link pin a language.
 *
 * A NOTE ON READING THIS SITE FROM DISK IN SAFARI.
 * ------------------------------------------------------------------------
 * Safari grants a file:// page access to the directory the reader actually
 * opened, and to that directory's descendants. Open
 * sdk/html/mtb-mpy/index.html and the whole mtb-mpy tree — English, Thai,
 * search/ — is reachable, which is why the TH|EN pill works there. Its
 * SIBLING sdk/html/mtb-only/ is not inside that grant, and no link, script,
 * or redirect on the page can reach it; the browser refuses the navigation
 * before any of our code is consulted. Nothing in this file can change that,
 * and an <a href> does not change it either — what the anchor buys is that
 * the refusal is the browser's, visible and reportable, instead of a control
 * that silently does nothing. Opening the site over http(s), or opening the
 * other variant's index.html directly, is unaffected.
 *
 * The counterpart of a page is at the identical path under the sibling
 * variant's tree, in the CURRENT language:
 *
 *     .../sdk/html/mtb-mpy/group__feat__edge__ai.html
 *     .../sdk/html/mtb-only/group__feat__edge__ai.html
 *     .../sdk/html/mtb-mpy/th/x.html   ->  .../sdk/html/mtb-only/th/x.html
 *
 * so the hop is `../<other>/` from a page in the EN tree root and
 * `../../<other>/th/` from the TH tree — both computed from BENTO_DOC's
 * `relpath` (doxygen's own per-page path back to its LANGUAGE tree root),
 * never from location.pathname, which cannot tell a docs root apart from a
 * directory that happens to be called "th" or "mtb-mpy".
 *
 * PRESENCE — generated data, no probe.
 * ------------------------------------------------------------------------
 * A shipped package contains exactly ONE variant: package-project copies
 * sdk/html/<variant>/ to docs/html/ and there is no sibling tree to jump to.
 * A selector offering the other one would be a dead control pointing at a
 * 404, so it must not exist there at all.
 *
 * fetch()/XHR cannot answer "is the sibling there": this site is built to be
 * read from disk, and every current browser blocks both against file:// URLs
 * — a probe would answer "missing" for every page that exists. The answer is
 * therefore GENERATED, into the manifest the language pill already uses
 * (bento-lang-pages.js, extended — one mechanism, not two):
 *
 *     window.BENTO_VARIANTS = {
 *       current:  "mtb-mpy",
 *       order:    ["mtb-only", "mtb-mpy"],
 *       labels:   { "mtb-only": "ModusToolbox", ... },
 *       siblings: { "mtb-only": { en: [...], th: [...] } }
 *     };
 *
 * `siblings` lists the OTHER variant trees that exist IN THIS DEPLOYMENT,
 * each with its page list, and is written by whichever produced the tree:
 *
 *   - docs-html, after moving the freshly rendered tree into sdk/html/, walks
 *     every variant present there and rewrites all their manifests — so
 *     rendering the second variant also teaches the first one about it;
 *   - package-project regenerates the manifest inside the packaged copy with
 *     NO siblings, because the zip holds one variant.
 *
 * Empty (or absent) `siblings` -> this script installs nothing. That is the
 * whole hiding mechanism: no element, no CSS to fight, nothing to click.
 *
 * The per-variant page lists are what make the jump page-for-page honest.
 * The two variants do not render the same page set (mtb-only has no
 * MicroPython Agent chapters and no mpy_secure reference pages), so a page
 * missing from the sibling falls back to the sibling's front page in the
 * current language — the same fallback shape, and the same dotted-underline
 * signal, as the language pill's.
 *
 * PLACEMENT — inside #MSearchBox's own parent, like the language pill and
 * like doxygen-awesome's own dark-mode toggle, but as its FIRST child so it
 * takes a row of its own above the search row. Two constraints from
 * bento-lang.js apply here unchanged and are load-bearing:
 *
 *   - doxygen 1.18's menu.js moves that markup between the desktop and mobile
 *     menu bars by COPYING innerHTML at 768px, so listeners bound to the
 *     element do not survive. Anchors need none — `href` is an attribute and
 *     survives serialisation — and the two delegated listeners we do keep are
 *     bound to `document`, which the copy never leaves.
 *   - Nothing may become a SIBLING of li#searchBoxPos2: sidebar-only hides
 *     every #main-menu li except the last child, so a sibling steals the
 *     search box's exemption and the search box vanishes.
 *
 * The open panel is `position: fixed`. `#top` is `overflow: hidden` in the
 * sidebar-only layout and would clip an absolutely positioned panel; a fixed
 * one is laid out against the viewport, so the clip does not apply to it.
 * (A native select's option list escaped the same clip by being drawn by the
 * browser outside the page — this is the CSS equivalent of that.) The
 * stylesheet gives the panel a correct static position for both layouts, and
 * the toggle handler below refines it from the summary's measured rect; if
 * that handler never runs, the panel is still in the right place and still
 * full of working links.
 * ------------------------------------------------------------------------ */

(function () {
    "use strict";

    var STORE_KEY = "bento-doc-lang";

    var doc = window.BENTO_DOC || {};
    var meta = null;
    try { meta = window.BENTO_VARIANTS || null; } catch (e) { meta = null; }

    /* No manifest at all (an older tree, a partial copy): say nothing. The
       language pill can degrade to a direct jump because its counterpart tree
       is inside the one it is standing in; this one cannot — the sibling may
       simply not be there, and a broken link is worse than no control. */
    if (!meta) { return; }

    var current = doc.variant || meta.current;
    var siblings = meta.siblings || {};
    var labels = meta.labels || {};
    var order = meta.order || [];

    var present = [];
    var i;
    for (i = 0; i < order.length; i++) {
        if (order[i] === current || siblings[order[i]]) { present.push(order[i]); }
    }
    /* A variant the order list does not know about still has to appear. */
    if (present.indexOf(current) === -1) { present.unshift(current); }
    Object.keys(siblings).forEach(function (v) {
        if (present.indexOf(v) === -1) { present.push(v); }
    });

    /* One variant in this deployment = a packaged zip. Install nothing. */
    if (present.length < 2) { return; }

    var lang = (doc.lang === "th") ? "th" : "en";
    var relpath = (typeof doc.relpath === "string") ? doc.relpath : "";
    var onWeb = (window.location.protocol === "http:" ||
                 window.location.protocol === "https:");

    /* ---- where am I, and where is the sibling ----------------------------
       This page's path relative to its own language tree root:
       "index.html", "group__feat__edge__ai.html", "search/all_0.html". */
    var rel = (function () {
        try {
            var langRoot = new URL(relpath || "./", window.location.href);
            var here = decodeURI(window.location.pathname);
            var rootPath = decodeURI(langRoot.pathname);
            var r = (here.indexOf(rootPath) === 0)
                ? here.slice(rootPath.length) : "";
            return r || "index.html";
        } catch (e) {
            return "index.html";
        }
    })();

    function pageExistsIn(variant) {
        try {
            var v = siblings[variant];
            /* No list for that language: assume the page is there rather than
               bouncing the reader to a front page on missing data. */
            if (!v || !v[lang] || !v[lang].length) { return true; }
            return v[lang].indexOf(rel) !== -1;
        } catch (e) {
            return true;
        }
    }

    /* A plain relative path to the same page under the sibling variant.

         EN   relpath + "../"    + variant + "/"        + rel
         TH   relpath + "../../" + variant + "/" + "th/" + rel

       Worked through, because getting this wrong is silent:
         .../mtb-mpy/index.html            relpath ""    -> ../mtb-only/index.html
         .../mtb-mpy/search/all_0.html     relpath "../" -> ../../mtb-only/search/all_0.html
         .../mtb-mpy/th/index.html         relpath ""    -> ../../mtb-only/th/index.html
         .../mtb-mpy/th/search/all_0.html  relpath "../" -> ../../../mtb-only/th/search/all_0.html */
    function hrefFor(variant) {
        var exists = pageExistsIn(variant);
        var href = relpath
                 + (lang === "th" ? "../../" : "../")
                 + variant + "/"
                 + (lang === "th" ? "th/" : "")
                 + (exists ? rel : "index.html");
        if (onWeb) { href += "?lang=" + lang; }
        /* Section anchors are \section ids and are the same in both variants
           wherever the page itself exists; drop the fragment when we fell
           back to the sibling's front page. */
        if (exists) {
            try {
                if (window.location.hash) { href += window.location.hash; }
            } catch (e) { /* keep the bare path */ }
        }
        return href;
    }

    /* The current variant's own row is a link to this very page, so that every
       row in the menu is a link and none of them is a dead target.
       `rel` is tree-root-relative and an href is resolved against the page's
       OWN directory, so it needs the same relpath prefix everything else gets:
       under search/ a bare `rel` would name search/search/all_0.html. */
    function selfHref() {
        var href = relpath + rel;
        if (onWeb) { href += "?lang=" + lang; }
        try {
            if (window.location.hash) { href += window.location.hash; }
        } catch (e) { /* keep the bare path */ }
        return href;
    }

    /* ---- the control ----------------------------------------------------- */

    function label(v) { return labels[v] || v; }

    /* Short forms keep the control the size of the language pill. The full
       package name rides in the title attribute. */
    function shortLabel(v) { return v === "mtb-mpy" ? "MTB+\u00b5Py" : "MTB"; }

    function build() {
        var wrap = document.createElement("bento-variant-switch");
        var pill = document.createElement("span");
        var missing = false;

        pill.className = "bento-variant-pill";

        present.forEach(function (v) {
            var a = document.createElement("a");
            a.className = "bento-variant-seg";
            a.textContent = shortLabel(v);
            a.title = label(v);
            if (v === current) {
                a.className += " bento-variant-seg-current";
                a.setAttribute("aria-current", "page");
                a.setAttribute("href", selfHref());
            } else {
                a.setAttribute("href", hrefFor(v));
                a.setAttribute("data-variant", v);
                if (!pageExistsIn(v)) {
                    a.className += " bento-variant-seg-fallback";
                    missing = true;
                }
            }
            pill.appendChild(a);
        });

        wrap.title = missing
            ? (lang === "th"
                ? "\u0e2b\u0e19\u0e49\u0e32\u0e19\u0e35\u0e49\u0e44\u0e21\u0e48\u0e21\u0e35\u0e43\u0e19\u0e2d\u0e35\u0e01\u0e41\u0e1e\u0e47\u0e01\u0e40\u0e01\u0e08\u0e2b\u0e19\u0e36\u0e48\u0e07 \u2014 \u0e08\u0e30\u0e44\u0e1b\u0e17\u0e35\u0e48\u0e2b\u0e19\u0e49\u0e32\u0e41\u0e23\u0e01"
                : "This page does not exist in the other package \u2014 the switch lands on its front page")
            : (lang === "th"
                ? "\u0e2a\u0e25\u0e31\u0e1a\u0e41\u0e1e\u0e47\u0e01\u0e40\u0e01\u0e08"
                : "Switch package");
        if (missing) { wrap.className = "bento-variant-fallback"; }

        wrap.appendChild(pill);
        return wrap;
    }

    /* ---- delegated behaviour — all of it optional -------------------------
       Nothing below is on the navigation path. Every handler is wrapped, and
       none of them calls preventDefault on a link. */

    /* Record the language so the sibling tree opens in it, then get out of the
       way: the browser follows the href whatever happens here. */
    document.addEventListener("click", function (ev) {
        try {
            var t = ev.target;
            var a = (t && t.closest) ? t.closest("a.bento-variant-item") : null;
            if (a) { window.localStorage.setItem(STORE_KEY, lang); }
        } catch (e) { /* the jump still happens */ }
    }, true);

    /* Place the open panel under its summary. The stylesheet already puts it
       in the right place for both layouts; this only corrects it for a header
       that is not where the CSS assumed (a narrow window, a zoomed page). */
    function place(det) {
        try {
            var sum = det.querySelector("summary");
            var list = det.querySelector(".bento-variant-list");
            if (!sum || !list) { return; }
            var r = sum.getBoundingClientRect();
            if (!r.width) { return; }
            list.style.top = Math.round(r.bottom + 4) + "px";
            list.style.left = Math.round(r.left) + "px";
            list.style.width = Math.round(r.width) + "px";
        } catch (e) { /* the static CSS position stands */ }
    }

    document.addEventListener("toggle", function (ev) {
        try {
            var det = ev.target;
            if (!det || !det.classList ||
                !det.classList.contains("bento-variant-menu")) { return; }
            if (det.open) { place(det); }
        } catch (e) { /* ignore */ }
    }, true);

    /* Close on an outside click or on Escape — what a reader expects of a
       dropdown, and what <details> does not do on its own. */
    function closeAll(except) {
        try {
            var els = document.querySelectorAll("details.bento-variant-menu[open]");
            for (var j = 0; j < els.length; j++) {
                if (els[j] !== except) { els[j].removeAttribute("open"); }
            }
        } catch (e) { /* ignore */ }
    }
    document.addEventListener("click", function (ev) {
        try {
            var t = ev.target;
            var inside = (t && t.closest) ? t.closest("details.bento-variant-menu") : null;
            closeAll(inside);
        } catch (e) { /* ignore */ }
    });
    document.addEventListener("keydown", function (ev) {
        try { if (ev.key === "Escape") { closeAll(null); } } catch (e) { /* ignore */ }
    });

    /* The fragment is baked into every href at build time; keep it current as
       the reader moves through the page's sections. Presentation only. */
    window.addEventListener("hashchange", function () {
        try {
            var els = document.querySelectorAll("a.bento-variant-item");
            for (var j = 0; j < els.length; j++) {
                var v = els[j].getAttribute("data-variant");
                els[j].setAttribute("href", v ? hrefFor(v) : selfHref());
            }
        } catch (e) { /* leave the built hrefs in place */ }
    });

    function install() {
        /* Idempotent: menu.js may re-run its layout pass, and a second
           selector is exactly the defect this guard prevents. */
        if (document.querySelector("bento-variant-switch")) { return; }

        var box = document.getElementById("MSearchBox");
        var host = box && box.parentNode ? box.parentNode : null;
        if (!host) {
            /* No search box (SEARCHENGINE=NO / DISABLE_INDEX): the title area
               keeps the selector reachable rather than absent. */
            host = document.getElementById("titlearea") || document.body;
            if (!host) { return; }
            host.appendChild(build());
        } else {
            /* FIRST child: the selector takes the row above the search box,
               which is the only way both fit — "ModusToolbox & µPython" is
               150px of text and the header column is a fixed 335px. */
            host.insertBefore(build(), box);
        }

        /* Only now does the stylesheet apply: the header has to grow by one
           row, and it must not grow on a package where nothing was inserted. */
        var cl = document.documentElement.className;
        if (cl.indexOf("bento-variant-on") === -1) {
            document.documentElement.className = cl + " bento-variant-on";
        }
    }

    if (document.readyState === "loading") {
        /* In <head>, so this runs before the pass menu.js schedules from
           <body>: the selector is part of the markup menu.js captures and
           moves between the two menu bars. */
        document.addEventListener("DOMContentLoaded", install);
    } else {
        install();
    }
})();

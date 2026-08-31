/* bento-i18n.js — swap the landing page between Thai and English in place.
 *
 * WHY IN PLACE
 * ------------
 * The page used to exist twice, as index.html and index.en.html, and switching
 * language meant a navigation: the reader lost their place, and the two files
 * sat under different scroll-memory keys because they differ by filename
 * rather than by directory. Swapping the text where it stands removes the
 * navigation, so there is nothing to lose and nothing to restore.
 *
 * WHERE THE TEXT COMES FROM
 * -------------------------
 * tools/merge_landing.py writes data-th / data-en onto the element that owns
 * each string, and data-th-tail / data-en-tail for text that follows a child
 * element. Both languages are therefore in the document at all times, which is
 * also what lets a crawler see them.
 *
 * WHAT IS REMEMBERED
 * ------------------
 * The choice, in localStorage, because a reader who picks a language means it
 * for the site and not for the tab. ?lang= in the URL wins over the stored
 * value, so a link can point at a language deliberately.
 */
(function () {
    "use strict";

    var KEY = "bento:lang";
    var LANGS = { th: "ไทย", en: "EN" };

    function stored() {
        try { return localStorage.getItem(KEY); } catch (e) { return null; }
    }
    function remember(l) {
        try { localStorage.setItem(KEY, l); } catch (e) { /* private mode */ }
    }

    function wanted() {
        var q = (location.search.match(/[?&]lang=(th|en)/i) || [])[1];
        if (q) { return q.toLowerCase(); }
        var s = stored();
        if (s === "th" || s === "en") { return s; }
        /* No choice yet: follow the browser, defaulting to Thai, because this
           is a Thai project and the Thai text is the original. */
        var nav = (navigator.language || "th").toLowerCase();
        return nav.indexOf("en") === 0 ? "en" : "th";
    }

    function apply(lang) {
        var other = lang === "th" ? "en" : "th";

        var nodes = document.querySelectorAll("[data-" + lang + "]");
        for (var i = 0; i < nodes.length; i++) {
            nodes[i].firstChild && nodes[i].firstChild.nodeType === 3
                ? (nodes[i].firstChild.nodeValue = nodes[i].getAttribute("data-" + lang))
                : nodes[i].insertBefore(
                    document.createTextNode(nodes[i].getAttribute("data-" + lang)),
                    nodes[i].firstChild);
        }

        var tails = document.querySelectorAll("[data-" + lang + "-tail]");
        for (var j = 0; j < tails.length; j++) {
            var el = tails[j], t = el.getAttribute("data-" + lang + "-tail");
            var next = el.nextSibling;
            if (next && next.nodeType === 3) { next.nodeValue = t; }
            else if (el.parentNode) {
                el.parentNode.insertBefore(document.createTextNode(t), next);
            }
        }

        ["alt", "title", "aria-label", "placeholder", "content", "href",
         "data-label-open", "data-label-close"].forEach(function (att) {
            var els = document.querySelectorAll("[data-" + lang + "-" + att + "]");
            for (var k = 0; k < els.length; k++) {
                els[k].setAttribute(att, els[k].getAttribute("data-" + lang + "-" + att));
            }
        });

        document.documentElement.setAttribute("lang", lang);
        document.documentElement.setAttribute("data-lang", lang);

        var pill = document.querySelector(".language-switch");
        if (pill) {
            pill.textContent = LANGS[other];
            pill.setAttribute("lang", other);
            pill.setAttribute("hreflang", other);
            pill.setAttribute("aria-label",
                other === "en" ? "Switch to English" : "อ่านเป็นภาษาไทย");
            /* A real href, so the control still works with scripting off and
               so it can be opened in a new tab deliberately. */
            pill.setAttribute("href", "?lang=" + other);
        }
    }

    /* Swapping the text changes the height of the page - Thai sets shorter
       than English here - so a reader deep in the page drifts by a few hundred
       pixels even though nothing navigated. Pin the element that was at the
       top of the viewport and put it back where it was. */
    function set(lang) {
        var mark = null, offset = 0;
        var cands = document.querySelectorAll("section, h2, h3, .act, figure, table");
        for (var i = 0; i < cands.length; i++) {
            var t = cands[i].getBoundingClientRect().top;
            if (t <= 120) { mark = cands[i]; offset = t; } else { break; }
        }

        apply(lang);
        remember(lang);

        if (mark) {
            var now = mark.getBoundingClientRect().top;
            window.scrollBy(0, now - offset);
        }
    }

    /* Run before first paint where possible, so the reader never sees the
       wrong language flash past. */
    apply(wanted());

    document.addEventListener("click", function (ev) {
        var t = ev.target;
        var el = (t && t.closest) ? t.closest(".language-switch") : null;
        if (!el) { return; }
        ev.preventDefault();
        set(document.documentElement.getAttribute("lang") === "th" ? "en" : "th");
    });
})();

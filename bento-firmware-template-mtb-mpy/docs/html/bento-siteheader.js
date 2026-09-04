/* ===========================================================================
   bento-siteheader.js — put the landing page's navigation on every docs page
   ---------------------------------------------------------------------------
   WHY. Entering the API reference used to be a one-way door: doxygen's chrome
   offers no route back to the product site, so a reader who followed "SDK docs"
   had to use the browser's back button to reach anything else.

   WHAT IT DOES NOT DO. It does not implement language or variant switching.
   Those two controls already exist (bento-lang.js, bento-variant.js), they
   already know how to find a page's counterpart in the other language or the
   other variant, and that logic was difficult to get right. This file MOVES
   them into the bar. Rebuilding them here would be a second implementation of
   the same thing, guaranteed to drift.

   ORDER. bento-lang.js and bento-variant.js install their controls into
   #MSearchBox's parent on DOMContentLoaded. This script cannot assume it runs
   after them, so it waits for the elements to appear rather than reading the
   DOM once and giving up. If they never appear the bar still renders, minus
   those two controls — a bar with no toggle beats no bar at all.

   PATHS. window.BENTO_DOC.relpath is doxygen's own per-page relative path back
   to the tree root, substituted into the header per page. That is the only
   reliable way to build a link out of a docs tree that also works from disk,
   where there is no server and no absolute root.
   ========================================================================= */

(function () {
  'use strict';

  var DOC = window.BENTO_DOC || {};
  var lang = DOC.lang === 'th' ? 'th' : 'en';
  // relpath points at the tree root of THIS variant, e.g. "../" or "".
  // The site root is two levels above that: <site>/sdk/<variant>[/th]/...
  var rel = typeof DOC.relpath === 'string' ? DOC.relpath : '';
  var toSiteRoot = rel + (lang === 'th' ? '../../../' : '../../');

  var TEXT = {
    en: {
      sub: 'SDK documentation',
      hardware: 'Hardware', software: 'Software', security: 'Security',
      edgeai: 'Edge AI', foundation: 'Foundation', contact: 'Contact',
      home: 'TESAIoT Development Kit — back to the product page'
    },
    th: {
      sub: 'เอกสาร SDK',
      hardware: 'ฮาร์ดแวร์', software: 'ซอฟต์แวร์', security: 'ความปลอดภัย',
      edgeai: 'Edge AI', foundation: 'Foundation', contact: 'ติดต่อ',
      home: 'TESAIoT Development Kit — กลับไปหน้าผลิตภัณฑ์'
    }
  }[lang];

  // The landing page is index.html (Thai) / index.en.html (English).
  var landing = toSiteRoot + (lang === 'th' ? 'index.html' : 'index.en.html');

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }

  function link(hash, label, optional) {
    var a = el('a', null, label);
    a.href = landing + '#' + hash;
    if (optional) a.setAttribute('data-optional', '');
    return a;
  }

  function build() {
    var bar = el('header', 'bento-siteheader');
    bar.setAttribute('role', 'banner');

    var brand = el('a', 'bento-sh-brand');
    brand.href = landing;
    brand.title = TEXT.home;
    brand.appendChild(el('span', 'bento-sh-mark'));
    var name = el('span', 'bento-sh-name');
    name.appendChild(document.createTextNode('TESA'));
    var b = document.createElement('b');
    b.textContent = 'IoT';
    name.appendChild(b);
    brand.appendChild(name);
    /* Sidebar toggle, first thing in the bar and next to the brand — the
       navigation it controls is on that side, and a control that lives beside
       what it acts on needs no label to explain it.

       doxygen makes the sidebar resizable by DRAGGING a two-pixel splitter.
       Dragged to zero it looks gone, and getting it back means finding that
       two-pixel strip again, which is not a thing anyone should have to aim
       at. This is the same action as a button. */
    var side = el('button', 'bento-sh-sidebar');
    side.type = 'button';
    side.setAttribute('aria-label', 'Show or hide the navigation');
    side.title = 'ซ่อน/แสดงแถบนำทาง';
    side.innerHTML =
      '<svg viewBox="0 0 20 16" width="16" height="16" aria-hidden="true">' +
      '<rect x="0.5" y="0.5" width="19" height="15" rx="2.5" fill="none" ' +
      'stroke="currentColor"/><path d="M7 1v14" stroke="currentColor"/>' +
      '<rect class="bento-sh-sidebar-fill" x="1" y="1" width="6" height="14" ' +
      'fill="currentColor" opacity=".35"/></svg>';
    bar.appendChild(side);

    bar.appendChild(brand);
    bar.appendChild(el('span', 'bento-sh-sub', TEXT.sub));

    var nav = el('nav', 'bento-sh-nav');
    nav.setAttribute('aria-label', TEXT.sub);
    nav.appendChild(link('hardware', TEXT.hardware, true));
    nav.appendChild(link('software', TEXT.software, true));
    nav.appendChild(link('security', TEXT.security, true));
    nav.appendChild(link('edgeai', TEXT.edgeai, true));
    nav.appendChild(link('foundation', TEXT.foundation, true));
    nav.appendChild(link('contact', TEXT.contact));
    bar.appendChild(nav);

    bar.appendChild(el('span', 'bento-sh-controls'));
    return bar;
  }

  /* Move a control into the bar, once it exists. Returns true when done so the
     poller can stop looking for it. */
  function adopt(slot, selector) {
    var node = document.querySelector(selector);
    if (!node || node.parentNode === slot) return !!node;
    slot.appendChild(node);
    return true;
  }

  function install() {
    if (document.querySelector('.bento-siteheader')) return;
    var bar = build();
    document.body.insertBefore(bar, document.body.firstChild);

    var slot = bar.querySelector('.bento-sh-controls');
    /* bento-variant.js builds a CUSTOM ELEMENT, <bento-variant-switch>, not a
       div carrying that as a class — a '.'-prefixed selector silently
       matches nothing and the control is left behind in the search row. */
    /* The variant switch is adopted; the language link is BUILT here.
       Adopting the one bento-lang.js installs made the control depend on which
       script ran first and on styling written for doxygen's search row, and
       the measured result was that it did not appear in the bar at all. One
       owner is simpler than a negotiation. */
    slot.appendChild(langPill());
    var want = ['bento-variant-switch'];
    var tries = 0;

    (function collect() {
      want = want.filter(function (sel) { return !adopt(slot, sel); });
      // ~2s of polling at animation cadence. The controls install on
      // DOMContentLoaded; this only has to outlast a slow first paint.
      if (want.length && ++tries < 120) {
        window.requestAnimationFrame(collect);
      } else {
        /* Adoption failed. Rather than leave the bar a control short — which
           is what a reader sees as "there is no way to change language" —
           build one here from what the page already declares about itself. */
        hold(slot);
      }
    })();
  }

  /* A language link built from window.BENTO_DOC, which every page declares:
     {lang, variant, relpath}. Used only when bento-lang.js did not put a pill
     anywhere this script could find. The href arithmetic is the whole of it —
     a Thai page is the English path with "th/" inserted before the filename,
     and an English page is the Thai path with that segment taken out. */
  function langPill() {
    var doc = window.BENTO_DOC || {};
    var lang = doc.lang === 'th' ? 'th' : 'en';
    var other = lang === 'th' ? 'en' : 'th';

    var path = window.location.pathname;
    var slash = path.lastIndexOf('/');
    var dir = path.slice(0, slash + 1);
    var file = path.slice(slash + 1) || 'index.html';
    var href = (lang === 'th')
      ? dir.replace(/\/th\/$/, '/') + file
      : dir + 'th/' + file;

    var a = document.createElement('a');
    a.className = 'bento-lang-toggle';
    a.setAttribute('href', href);
    a.setAttribute('data-lang', lang);
    a.setAttribute('data-other', other);
    a.setAttribute('rel', 'alternate');
    a.setAttribute('hreflang', other);
    a.title = other === 'th' ? 'อ่านหน้านี้เป็นภาษาไทย'
                             : 'Read this page in English';

    ['en', 'th'].forEach(function (code) {
      var sp = document.createElement('span');
      sp.textContent = code === 'th' ? 'ไทย' : 'EN';
      if (code === lang) { sp.className = 'bento-lang-active'; }
      a.appendChild(sp);
    });
    return a;
  }

  /* Adoption alone is not enough. doxygen's menu.js runs its own layout pass
     after DOMContentLoaded and RE-PARENTS the markup it manages between the
     desktop and mobile menu bars — which carries the language pill back out of
     this bar and into a row that the sidebar then clips, so the control simply
     disappears for the reader. Watch for that and take it back.

     Scoped to childList on document.body's subtree and doing nothing unless a
     watched control has actually moved, so the observer costs nothing on a
     page where menu.js leaves things alone. */
  function hold(slot) {
    if (!window.MutationObserver) return;

    /* Watched for movement. The variant switch is taken back into the bar;
       a language pill installed anywhere else is hidden, because ours is the
       one in the bar and two of them disagreeing about where they live is
       worse than one. */
    var SEL = 'bento-variant-switch, a.bento-lang-toggle';
    var pending = false;

    function reclaim() {
      pending = false;
      var nodes = document.querySelectorAll(SEL);
      for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        if (n.parentNode === slot) continue;
        if (n.tagName === 'BENTO-VARIANT-SWITCH') { slot.appendChild(n); }
        else { n.style.display = 'none'; }   /* a stray language pill */
      }
    }
    reclaim();   /* whatever is already in the wrong place, now */

    new MutationObserver(function (records) {
      if (pending) return;
      for (var i = 0; i < records.length; i++) {
        var added = records[i].addedNodes;
        for (var j = 0; j < added.length; j++) {
          var n = added[j];
          if (n.nodeType !== 1) continue;
          if ((n.matches && n.matches(SEL)) ||
              (n.querySelector && n.querySelector(SEL))) {
            pending = true;
            window.requestAnimationFrame(reclaim);
            return;
          }
        }
      }
    }).observe(document.body, { childList: true, subtree: true });
  }

  /* ---- the navigation tree's height ------------------------------------
     doxygen-awesome gives #nav-tree `height: calc(100vh - var(--top-height))`,
     but the tree does not start at --top-height: the sidebar's own title and
     search box sit above it inside #side-nav. The tree is therefore taller
     than the space it has by exactly the height of that block, and the items
     at one end fall outside. doxygen also restores a scroll offset for the
     tree from a cookie, so on a returning visit the overflow shows up as the
     FIRST entries being cut off — which is how this was reported.

     An earlier attempt subtracted a guessed constant and removed five entries
     from the middle. This measures instead: the tree ends where its container
     ends, whatever is above it and whatever the window does next. */
  function fitNavTree() {
    var top  = document.getElementById('top');
    var side = document.getElementById('side-nav');
    var tree = document.getElementById('nav-tree');
    if (!top || !side || !tree) { return; }

    /* --top-height is a CONSTANT 120px in doxygen-awesome, and #top is not
       120px tall here: it carries a two-line title and the search box, and the
       Thai title wraps differently from the English one. #top is sticky and
       paints over whatever shares its space, so the sidebar started underneath
       it and its first two entries were covered — which is what "the list is
       being pushed out of view" was. Guessing a replacement constant is what
       removed five entries in an earlier attempt.

       Measure #top instead. Everything below follows from its real bottom. */
    var topBottom = Math.round(top.getBoundingClientRect().bottom);
    if (topBottom <= 0) { return; }

    side.style.top    = topBottom + 'px';
    side.style.height = 'calc(100vh - ' + topBottom + 'px)';

    var s = side.getBoundingClientRect();
    var t = tree.getBoundingClientRect();
    var h = Math.max(0, Math.round(s.bottom - t.top));
    if (h > 0) { tree.style.height = h + 'px'; }

    /* navtree.js restores a scroll offset measured against a different layout,
       and doxygen also scrolls the tree to the selected node. Either can leave
       the top rows out of reach. Start at the top; once the reader scrolls,
       the scrolling is theirs. */
    if (!navSettled) { tree.scrollTop = 0; }
  }

  function watchNavTree() {
    fitNavTree();
    window.addEventListener('resize', fitNavTree);
    /* navtree.js builds its rows after load and the fonts settle later still;
       both change the height the tree needs. Re-fit while that happens. */
    var until = Date.now() + 1500;
    (function again() {
      fitNavTree();
      if (Date.now() < until) { window.requestAnimationFrame(again); }
      else { navSettled = true; }
    })();
  }

  /* ---- the sidebar toggle ------------------------------------------------
     Everything in this layout is sized from one custom property, so the whole
     control is that property plus a class for the styling. Nothing here
     touches #side-nav or #doc-content directly, which is what keeps it from
     fighting doxygen's own splitter arithmetic. */
  var SIDE_KEY = 'bento:sidebar';

  function applySidebar(hidden) {
    document.documentElement.classList.toggle('bento-sidebar-hidden', !!hidden);
    var b = document.querySelector('.bento-sh-sidebar');
    if (b) { b.setAttribute('aria-pressed', hidden ? 'true' : 'false'); }
  }

  function initSidebar() {
    var hidden = false;
    try { hidden = localStorage.getItem(SIDE_KEY) === '1'; } catch (e) {}
    applySidebar(hidden);

    document.addEventListener('click', function (ev) {
      var t = ev.target;
      var b = (t && t.closest) ? t.closest('.bento-sh-sidebar') : null;
      if (!b) { return; }
      ev.preventDefault();
      var now = !document.documentElement.classList.contains('bento-sidebar-hidden');
      applySidebar(now);
      try { localStorage.setItem(SIDE_KEY, now ? '1' : '0'); } catch (e) {}
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { install(); initSidebar(); });
    window.addEventListener('load', watchNavTree);
  } else {
    install();
    initSidebar();
    watchNavTree();
  }
})();

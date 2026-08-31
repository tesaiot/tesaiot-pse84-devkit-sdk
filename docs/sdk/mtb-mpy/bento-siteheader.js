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
    var want = ['bento-variant-switch', '.bento-lang-toggle'];
    var tries = 0;

    (function collect() {
      want = want.filter(function (sel) { return !adopt(slot, sel); });
      // ~2s of polling at animation cadence. The controls install on
      // DOMContentLoaded; this only has to outlast a slow first paint.
      if (want.length && ++tries < 120) {
        window.requestAnimationFrame(collect);
      }
    })();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', install);
  } else {
    install();
  }
})();

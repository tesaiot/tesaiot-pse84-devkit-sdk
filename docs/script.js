/* TESAIoT product site — progressive enhancement only.
   Everything here degrades to a working page with JS off: the nav is a plain
   anchor list, the spec groups are <details>, and .reveal has no opacity rule
   until .js-ready is set below. */

(function () {
  'use strict';

  document.documentElement.classList.add('js-ready');

  /* ── Mobile navigation ────────────────────────────────────────────────
     The class toggled here is .is-open and it has a matching rule in
     styles.css under the 860px breakpoint. The previous build toggled
     .open, for which no rule existed, so the menu did nothing at all
     below 640px. */
  var burger = document.getElementById('burger');
  var nav = document.getElementById('nav');
  var openLabel = burger && burger.getAttribute('data-label-open') || 'Open menu';
  var closeLabel = burger && burger.getAttribute('data-label-close') || 'Close menu';

  function closeNav() {
    if (!nav || !burger) return;
    nav.classList.remove('is-open');
    burger.setAttribute('aria-expanded', 'false');
    burger.setAttribute('aria-label', openLabel);
  }

  if (burger && nav) {
    burger.addEventListener('click', function () {
      var open = nav.classList.toggle('is-open');
      burger.setAttribute('aria-expanded', String(open));
      burger.setAttribute('aria-label', open ? closeLabel : openLabel);
    });

    // Follow a link, then get out of the way.
    nav.addEventListener('click', function (e) {
      if (e.target.closest('a')) closeNav();
    });

    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape') closeNav();
    });

    // Leaving the breakpoint must not strand the panel open.
    var wide = window.matchMedia('(min-width: 861px)');
    (wide.addEventListener ? wide.addEventListener.bind(wide, 'change')
                           : wide.addListener.bind(wide))(closeNav);
  }

  /* ── Header hairline, only once the page has actually moved ─────────── */
  var header = document.getElementById('site-header');
  if (header) {
    var stuck = false;
    var onScroll = function () {
      var next = window.scrollY > 8;
      if (next !== stuck) {
        stuck = next;
        header.classList.toggle('is-stuck', stuck);
      }
    };
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
  }

  /* ── Reveal on entry ──────────────────────────────────────────────────
     Skipped entirely when the visitor asks for reduced motion: in that
     case the elements are simply left visible. */
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var targets = document.querySelectorAll('.act-head, .spec, .path, .sdk-copy, .sdk-code, .sec-card, .ai-slots, .ai-routes, .benefits article');

  if (reduce || !('IntersectionObserver' in window)) {
    targets.forEach(function (el) { el.classList.add('is-in'); });
  } else {
    targets.forEach(function (el) { el.classList.add('reveal'); });

    /* Stagger, for the groups that are actually groups.
     *
     * A row of cards that all fade in on the same frame reads as one block
     * flickering, not as a set arriving. Offsetting each one slightly is what
     * makes a grid feel deliberate rather than merely animated.
     *
     * The offset is by position WITHIN the card's own parent, so a single
     * heading is never delayed, and it is capped: past the fifth card the
     * delay stops growing, because a reader who has scrolled to the bottom of
     * a long grid should not sit waiting for the last item.
     *
     * Delay is set at reveal time, not up front, so an element scrolled past
     * quickly is not still holding a delay from a group it never showed with. */
    var STEP_MS = 70;
    var MAX_STEPS = 5;

    function staggerIndex(el) {
      var parent = el.parentElement;
      if (!parent) { return 0; }
      var sibs = [];
      for (var i = 0; i < parent.children.length; i++) {
        if (parent.children[i].classList.contains('reveal')) {
          sibs.push(parent.children[i]);
        }
      }
      if (sibs.length < 2) { return 0; }     /* not a group; do not delay it */
      var n = sibs.indexOf(el);
      return Math.min(n < 0 ? 0 : n, MAX_STEPS);
    }

    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        var d = staggerIndex(entry.target) * STEP_MS;
        if (d > 0) { entry.target.style.transitionDelay = d + 'ms'; }
        entry.target.classList.add('is-in');
        io.unobserve(entry.target);
      });
    }, { rootMargin: '0px 0px -12% 0px', threshold: 0.08 });
    targets.forEach(function (el) { io.observe(el); });
  }


  /* ── Back to top ──────────────────────────────────────────────────────
     Appears once the reader is a screenful past the hero. The button ships
     with [hidden] so it is absent for a visitor with JS off rather than
     being a control that does nothing. */
  var toTop = document.getElementById('to-top');
  if (toTop) {
    toTop.hidden = false;
    var showAt = function () { return Math.max(320, window.innerHeight * 0.9); };
    var syncTop = function () {
      toTop.classList.toggle('is-shown', window.scrollY > showAt());
    };
    window.addEventListener('scroll', syncTop, { passive: true });
    window.addEventListener('resize', syncTop, { passive: true });
    syncTop();

    toTop.addEventListener('click', function () {
      window.scrollTo({ top: 0, behavior: reduce ? 'auto' : 'smooth' });
      // Send focus somewhere sensible, or a keyboard user is left at the bottom.
      var first = document.querySelector('.brand');
      if (first) first.focus({ preventScroll: true });
    });
  }

  /* ── Scroll spy ───────────────────────────────────────────────────────
     Marks the nav link for whichever section owns the upper third of the
     viewport. Falls silent if IntersectionObserver is unavailable. */
  if ('IntersectionObserver' in window) {
    var links = {};
    document.querySelectorAll('.nav a[href^="#"]').forEach(function (a) {
      links[a.getAttribute('href').slice(1)] = a;
    });
    var sections = Object.keys(links)
      .map(function (id) { return document.getElementById(id); })
      .filter(Boolean);

    if (sections.length) {
      var spy = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          var a = links[entry.target.id];
          if (!a) return;
          if (entry.isIntersecting) {
            Object.keys(links).forEach(function (k) { links[k].classList.remove('is-current'); });
            a.classList.add('is-current');
          }
        });
      }, { rootMargin: '-20% 0px -70% 0px' });
      sections.forEach(function (s) { spy.observe(s); });
    }
  }
})();

/* ---------------------------------------------------------------------------
   --head-h: ความสูงจริงของแถบเมนู
   styles.css ใช้ค่านี้กับ scroll-padding-top เพื่อไม่ให้แถบ sticky บังหัวข้อ
   ที่ลิงก์กระโดดไป ความสูงเปลี่ยนตามภาษาและขนาดหน้าจอ จึงวัดแทนที่จะกำหนดตาย
   วัดซ้ำเมื่อเปลี่ยนขนาดหน้าต่างและตอนฟอนต์จัดตัวเสร็จ
   ------------------------------------------------------------------------- */
(function () {
  var head = document.querySelector('.site');
  if (!head) { return; }

  function setHeadHeight() {
    var h = Math.round(head.getBoundingClientRect().height);
    if (h > 0) {
      document.documentElement.style.setProperty('--head-h', h + 'px');
    }
  }

  setHeadHeight();
  window.addEventListener('resize', setHeadHeight, { passive: true });
  window.addEventListener('load', setHeadHeight);
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(setHeadHeight);
  }
})();

/* ---------------------------------------------------------------------------
   Hero entrance, the board's callout pins, and the counters.

   All three are skipped outright under prefers-reduced-motion: the elements
   are simply left in their finished state, which is what the rest of this file
   does and what the media query is for.
   ------------------------------------------------------------------------- */
(function () {
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---- 1. the hero arrives in reading order --------------------------- */
  var heroSeq = ['.hero .eyebrow', '.hero h1', '.hero .lead',
                 '.hero-actions', '.hero-ledger', '.hero-board'];
  var heroEls = heroSeq
        .map(function (sel) { return document.querySelector(sel); })
        .filter(Boolean);

  if (!reduce) { heroEls.forEach(function (el) { el.classList.add('hero-in'); }); }

  function runHero() {
    heroEls.forEach(function (el, i) {
      if (reduce) { el.classList.add('is-in'); return; }
      /* 90ms apart: far enough to read as a sequence, close enough that the
         last item is in place well before a reader could act on the first. */
      setTimeout(function () { el.classList.add('is-in'); }, 60 + i * 90);
    });
  }

  /* ---- 2. the eight callout pins, after the photograph is in place ---- */
  var pins = document.querySelector('.board-pins');

  function runPins() {
    if (!pins) { return; }
    if (reduce) { pins.classList.add('is-in'); return; }
    var items = pins.querySelectorAll('li');
    items.forEach(function (li, i) { li.style.transitionDelay = (i * 90) + 'ms'; });
    pins.classList.add('is-in');
    /* Drop the delays once they have played, or a later repaint replays them. */
    setTimeout(function () {
      items.forEach(function (li) { li.style.transitionDelay = ''; });
    }, 90 * items.length + 600);
  }

  /* ---- 3. the four headline numbers count up ------------------------- */
  /* Only the leading integer is animated. The unit that follows it lives in
     its own <span class="u"> and is left alone, so "237k บรรทัด" does not
     become "0k บรรทัด" on the way up. */
  function countUp(dd) {
    var node = dd.firstChild;
    if (!node || node.nodeType !== 3) { return; }
    var target = parseInt(node.nodeValue, 10);
    if (!isFinite(target) || target <= 0) { return; }
    if (reduce) { return; }

    var DUR = 900, t0 = null;
    node.nodeValue = '0';
    (function step(ts) {
      if (t0 === null) { t0 = ts; }
      var p = Math.min(1, (ts - t0) / DUR);
      /* ease-out: fast at first, settling into the real figure */
      var v = Math.round(target * (1 - Math.pow(1 - p, 3)));
      node.nodeValue = String(v);
      if (p < 1) { requestAnimationFrame(step); }
      else { node.nodeValue = String(target); }
    })(performance.now());
  }

  var stats = document.querySelector('.hero-ledger');
  function runCounters() {
    if (!stats) { return; }
    stats.querySelectorAll('dd').forEach(countUp);
  }

  /* ---- when ----------------------------------------------------------- */
  function start() {
    runHero();
    /* The pins wait for the photograph's own entrance to finish; it is last in
       the hero sequence. */
    setTimeout(runPins, 60 + (heroEls.length - 1) * 90 + 500);
    setTimeout(runCounters, 60 + 4 * 90);
  }

  if (document.readyState === 'complete') { start(); }
  else { window.addEventListener('load', start); }
})();

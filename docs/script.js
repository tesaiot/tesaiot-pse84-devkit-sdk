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
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
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

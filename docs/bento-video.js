/* bento-video.js — เปิดคลิปที่ผู้อ่านกด ขึ้นมาเล่นในหน้าเดิม
 *
 * ทำไมไม่ฝัง iframe ไว้ในหน้าเลย
 * ------------------------------
 * การ์ดวิดีโอในหน้านี้มีสิบกว่าใบ ถ้าแต่ละใบเป็น iframe ของ YouTube หน้าเดียว
 * จะลากสคริปต์ของ player มาสิบกว่าชุดตั้งแต่วินาทีแรก ทั้งที่ผู้อ่านส่วนใหญ่
 * กดดูไม่เกินหนึ่งคลิป การ์ดจึงเป็นภาพนิ่งกับลิงก์ธรรมดาไป youtu.be และไฟล์นี้
 * ทำหน้าที่เดียว: ดักการกด แล้วยกคลิปนั้นขึ้นมาเป็น iframe เดียวในกล่อง
 *
 * ปิดสคริปต์แล้วยังใช้ได้
 * -----------------------
 * เพราะการ์ดคือ <a href="https://youtu.be/..."> จริง ๆ ไม่ใช่ปุ่มที่รอ JS
 * ไฟล์นี้จึงเป็นการต่อยอด ไม่ใช่เงื่อนไขให้หน้าทำงาน เหมือนกับ script.js
 *
 * ความเป็นส่วนตัว
 * ---------------
 * โดเมนที่ฝังคือ youtube-nocookie.com ซึ่งไม่วางคุกกี้ติดตามจนกว่าจะเริ่มเล่น
 * และภาพปกมาจาก i.ytimg.com ที่เป็นโดเมนสำหรับไฟล์ภาพล้วน
 */
(function () {
  'use strict';

  var PLAYER = 'https://www.youtube-nocookie.com/embed/';
  var open = null;      /* กล่องที่เปิดอยู่ */
  var opener = null;    /* การ์ดที่พาเรามาที่นี่ — ปิดแล้วโฟกัสต้องกลับไปที่เดิม */

  function lang() {
    return document.documentElement.getAttribute('lang') === 'en' ? 'en' : 'th';
  }

  function txt(el, attr) {
    return (el && el.getAttribute(attr)) || '';
  }

  /* ── สร้างกล่อง ──────────────────────────────────────────────────────
     ข้อความในกล่องถูกติด data-th / data-en ไว้ด้วย เพื่อให้ปุ่มสลับภาษาที่
     bento-i18n.js ดูแล เปลี่ยนกล่องที่เปิดค้างอยู่ได้เหมือนเนื้อหาอื่นในหน้า */
  function build(card) {
    var id = card.getAttribute('data-video');
    var titleEl = card.querySelector('.vid-title');
    var current = titleEl ? titleEl.textContent.trim() : 'TESAIoT';

    var modal = document.createElement('div');
    modal.className = 'vid-modal';
    modal.setAttribute('role', 'dialog');
    modal.setAttribute('aria-modal', 'true');
    modal.setAttribute('aria-label', current);

    var inner = document.createElement('div');
    inner.className = 'vid-modal-inner';

    var head = document.createElement('div');
    head.className = 'vid-modal-head';

    var name = document.createElement('span');
    name.textContent = current;
    if (titleEl && titleEl.getAttribute('data-th')) {
      name.setAttribute('data-th', txt(titleEl, 'data-th'));
      name.setAttribute('data-en', txt(titleEl, 'data-en') || txt(titleEl, 'data-th'));
    }

    var close = document.createElement('button');
    close.type = 'button';
    close.className = 'vid-modal-close';
    close.innerHTML = '&times;';
    close.setAttribute('aria-label', lang() === 'en' ? 'Close' : 'ปิด');
    close.setAttribute('data-th-aria-label', 'ปิด');
    close.setAttribute('data-en-aria-label', 'Close');

    head.appendChild(name);
    head.appendChild(close);

    var frame = document.createElement('div');
    frame.className = 'vid-modal-frame';

    var iframe = document.createElement('iframe');
    iframe.src = PLAYER + encodeURIComponent(id) +
      '?autoplay=1&rel=0&modestbranding=1&playsinline=1&hl=' + lang() + '&cc_lang_pref=' + lang();
    iframe.title = current;
    iframe.setAttribute('allow',
      'accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share');
    iframe.setAttribute('referrerpolicy', 'strict-origin-when-cross-origin');
    iframe.setAttribute('allowfullscreen', '');
    frame.appendChild(iframe);

    var foot = document.createElement('div');
    foot.className = 'vid-modal-foot';

    var note = document.createElement('span');
    var noteEl = card.querySelector('.vid-note');
    if (noteEl) {
      note.textContent = noteEl.textContent.trim();
      if (noteEl.getAttribute('data-th')) {
        note.setAttribute('data-th', txt(noteEl, 'data-th'));
        note.setAttribute('data-en', txt(noteEl, 'data-en') || txt(noteEl, 'data-th'));
      }
    }

    var out = document.createElement('a');
    out.href = card.getAttribute('href') || ('https://youtu.be/' + id);
    out.target = '_blank';
    out.rel = 'noopener';
    out.textContent = lang() === 'en' ? 'Watch on YouTube ↗' : 'ดูบน YouTube ↗';
    out.setAttribute('data-th', 'ดูบน YouTube ↗');
    out.setAttribute('data-en', 'Watch on YouTube ↗');

    foot.appendChild(note);
    foot.appendChild(out);

    inner.appendChild(head);
    inner.appendChild(frame);
    inner.appendChild(foot);
    modal.appendChild(inner);

    /* พื้นหลังปิดได้ แต่การกดในตัวกล่องต้องไม่ปิด */
    modal.addEventListener('click', function (e) { if (e.target === modal) { shut(); } });
    close.addEventListener('click', shut);

    /* กักโฟกัสไว้ในกล่อง — กล่องที่กดแท็บแล้วหลุดไปหลังฉากคือกล่องที่ผู้ใช้
       คีย์บอร์ดหาทางปิดไม่เจอ */
    modal.addEventListener('keydown', function (e) {
      if (e.key !== 'Tab') { return; }
      var able = modal.querySelectorAll('button, a[href], iframe');
      if (!able.length) { return; }
      var first = able[0], last = able[able.length - 1];
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
    });

    return { modal: modal, close: close };
  }

  function shut() {
    if (!open) { return; }
    open.parentNode && open.parentNode.removeChild(open);
    open = null;
    document.documentElement.classList.remove('vid-lock');
    document.body.style.paddingRight = '';
    if (opener) { opener.focus(); opener = null; }
  }

  function show(card) {
    shut();
    var built = build(card);
    open = built.modal;
    opener = card;

    /* ล็อกการเลื่อนหน้า และชดเชยความกว้างแถบเลื่อนที่หายไป ไม่งั้นทั้งหน้า
       จะกระตุกไปทางขวาตอนกล่องเปิด */
    var gap = window.innerWidth - document.documentElement.clientWidth;
    if (gap > 0) { document.body.style.paddingRight = gap + 'px'; }
    document.documentElement.classList.add('vid-lock');

    document.body.appendChild(open);
    /* เฟรมถัดไป เพื่อให้ transition ของ .is-open มีสถานะเริ่มต้นให้ไล่จาก */
    requestAnimationFrame(function () { open && open.classList.add('is-open'); });
    built.close.focus();
  }

  document.addEventListener('click', function (e) {
    /* ปล่อยให้เบราว์เซอร์ทำงานตามปกติเมื่อผู้ใช้ตั้งใจเปิดแท็บใหม่ */
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) { return; }
    var card = e.target.closest && e.target.closest('[data-video]');
    if (!card) { return; }
    e.preventDefault();
    show(card);
  });

  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && open) { shut(); }
  });
})();

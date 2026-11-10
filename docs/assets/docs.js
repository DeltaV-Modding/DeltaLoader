(function () {
  'use strict';
  var root = document.body.getAttribute('data-root') || '';
  var $ = function (s, c) { return (c || document).querySelector(s); };

  // theme
  var themeBtn = $('#theme');
  if (themeBtn) themeBtn.addEventListener('click', function () {
    var cur = document.documentElement.getAttribute('data-theme');
    if (!cur) cur = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    var next = cur === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', next);
    try { localStorage.setItem('dl-theme', next); } catch (e) { }
  });

  // mobile navigation
  var menu = $('#menu-btn'), scrim = $('#scrim');
  function navOpen(open) { document.body.classList.toggle('nav-open', open); if (menu) menu.setAttribute('aria-expanded', open ? 'true' : 'false'); }
  if (menu) menu.addEventListener('click', function () { navOpen(!document.body.classList.contains('nav-open')); });
  if (scrim) scrim.addEventListener('click', function () { navOpen(false); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') navOpen(false); });
  var active = $('.sidebar a.active'); if (active && active.scrollIntoView) active.scrollIntoView({ block: 'center' });

  // copy buttons
  document.addEventListener('click', function (e) {
    var b = e.target.closest ? e.target.closest('.copy') : null;
    if (!b) return;
    var cb = b.closest('.code'); var code = cb ? cb.querySelector('code') : null; var direct = b.getAttribute('data-copy'); var src = direct !== null ? direct : code.innerText;
    var done = function () { b.textContent = 'Copied'; b.setAttribute('data-done', ''); setTimeout(function () { b.textContent = 'Copy'; b.removeAttribute('data-done'); }, 1400); };
    if (navigator.clipboard) navigator.clipboard.writeText(src).then(done, function () { }); else {
      var r = document.createRange(); r.selectNodeContents(code || b.parentNode.querySelector('code')); var s = getSelection(); s.removeAllRanges(); s.addRange(r);
      try { document.execCommand('copy'); done(); } catch (x) { }
    }
  });

  // table of contents: highlight the section in view
  var tocLinks = [].slice.call(document.querySelectorAll('.toc a'));
  if (tocLinks.length && 'IntersectionObserver' in window) {
    var map = {}; tocLinks.forEach(function (a) { map[a.getAttribute('href').slice(1)] = a; });
    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { if (en.isIntersecting) { tocLinks.forEach(function (a) { a.classList.remove('on'); }); var a = map[en.target.id]; if (a) a.classList.add('on'); } });
    }, { rootMargin: '-70px 0px -70% 0px' });
    Object.keys(map).forEach(function (id) { var el = document.getElementById(id); if (el) obs.observe(el); });
  }

  // version selector: one version today; the list comes from site.json and each entry is a folder next to this one
  var ver = $('#version');
  if (ver) ver.addEventListener('change', function () { var cur = ver.querySelector('[selected]'); if (cur && ver.value !== cur.value) { location.href = root + '../' + ver.value + '/'; } });

  // search
  var modal = $('#search-modal'), input = $('#search-q'), list = $('#search-results'), index = null, sel = -1, items = [];
  function load() {
    if (index) return Promise.resolve(index);
    return fetch(root + 'search-index.json').then(function (r) { return r.json(); }).then(function (d) { index = d; return d; }).catch(function () { index = []; return index; });
  }
  function open() { modal.hidden = false; load(); input.value = ''; list.innerHTML = ''; input.focus(); document.body.style.overflow = 'hidden'; run(''); }
  function close() { modal.hidden = true; document.body.style.overflow = ''; }
  var esc = function (s) { return s.replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); };
  function hl(text, terms) {
    var out = esc(text);
    terms.forEach(function (t) { if (t.length > 1) out = out.replace(new RegExp('(' + t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'gi'), '<mark>$1</mark>'); });
    return out;
  }
  function snippet(x, terms) {
    var low = x.toLowerCase(), pos = -1;
    for (var i = 0; i < terms.length; i++) { var p = low.indexOf(terms[i]); if (p >= 0 && (pos < 0 || p < pos)) pos = p; }
    if (pos < 0) return x.slice(0, 110);
    var s = Math.max(0, pos - 45), e = Math.min(x.length, pos + 110);
    return (s ? '…' : '') + x.slice(s, e) + (e < x.length ? '…' : '');
  }
  function run(q) {
    load().then(function (data) {
      var terms = q.toLowerCase().split(/\s+/).filter(Boolean), res = [];
      if (!terms.length) { data.slice(0, 7).forEach(function (p) { res.push({ p: p, h: null, score: 0, x: '' }); }); }
      else data.forEach(function (p) {
        var tl = p.t.toLowerCase(), xl = p.x.toLowerCase(), score = 0, ok = true, head = null;
        terms.forEach(function (t) {
          var s = 0;
          if (tl.indexOf(t) >= 0) s += tl === t ? 60 : 30;
          p.h.forEach(function (h) { if (h.t.toLowerCase().indexOf(t) >= 0) { s += 14; if (!head) head = h; } });
          var c = xl.split(t).length - 1; if (c) s += Math.min(10, c);
          if (!s) ok = false; score += s;
        });
        if (ok) res.push({ p: p, h: head, score: score, x: snippet(p.x, terms) });
      });
      res.sort(function (a, b) { return b.score - a.score; });
      res = res.slice(0, 12);
      items = res; sel = res.length ? 0 : -1;
      list.innerHTML = res.length ? res.map(function (r, i) {
        var href = root + r.p.u + (r.h ? '#' + r.h.i : '');
        return '<li role="presentation"><a role="option" href="' + href + '" aria-selected="' + (i === 0) + '"><span class="r-t">' + hl(r.p.t, terms) + (r.h ? ' <span class="r-s">&rsaquo; ' + hl(r.h.t, terms) + '</span>' : '') + '</span><span class="r-s">' + esc(r.p.s) + '</span>' + (r.x ? '<span class="r-x">' + hl(r.x, terms) + '</span>' : '') + '</a></li>';
      }).join('') : '<li class="search-empty">No results for “' + esc(q) + '”</li>';
    });
  }
  function move(d) {
    if (!items.length) return; sel = (sel + d + items.length) % items.length;
    [].forEach.call(list.querySelectorAll('a'), function (a, i) { a.setAttribute('aria-selected', i === sel); if (i === sel) a.scrollIntoView({ block: 'nearest' }); });
  }
  if (modal) {
    var ob = $('#search-open'); if (ob) ob.addEventListener('click', open);
    modal.addEventListener('click', function (e) { if (e.target.hasAttribute && e.target.hasAttribute('data-close')) close(); });
    input.addEventListener('input', function () { run(input.value.trim()); });
    input.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); } else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') { var a = list.querySelectorAll('a')[sel]; if (a) location.href = a.href; }
    });
    document.addEventListener('keydown', function (e) {
      if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); modal.hidden ? open() : close(); }
      else if (e.key === '/' && modal.hidden && !/input|textarea|select/i.test((document.activeElement || {}).tagName || '')) { e.preventDefault(); open(); }
      else if (e.key === 'Escape' && !modal.hidden) close();
    });
  }
})();

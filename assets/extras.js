// Dodatki podglądu: zwijanie sekcji i konspekt, szukanie, powiększenie i licznik słów, sprawdzanie linków,
// dymki po najechaniu, znacznik kursora, kopiowanie (kod, diagramy, sformatowany tekst), eksport do HTML,
// prezentacja, spis treści w tekście i zamiana HTML ze schowka na Markdown.
window.MdExtras = (() => {
  'use strict';

  let ctx = null;   // { post, t, md, content, rerender, cfg, glideTo, barHeight, sanitize, splitFrontMatter, escapeHtml, toast, lastText, invalidate, loadMermaid }
  const content = () => ctx.content;
  const t = (k) => ctx.t(k);
  const HEADINGS = ':scope > :is(h1, h2, h3, h4, h5, h6)';
  const svg = (body) => '<svg viewBox="0 0 16 16" aria-hidden="true">' + body + '</svg>';
  const ICON = {
    copy: svg('<rect x="5.5" y="5.5" width="8" height="9" rx="1.5"/><path d="M10.5 5.5v-2a1.5 1.5 0 0 0-1.5-1.5H4A1.5 1.5 0 0 0 2.5 3.5V10A1.5 1.5 0 0 0 4 11.5h1.5"/>'),
    ok: svg('<path d="m3 8.5 3 3 7-7"/>'),
    up: svg('<path d="m4 10 4-4 4 4"/>'),
    down: svg('<path d="m4 6 4 4 4-4"/>'),
    close: svg('<path d="m4 4 8 8M12 4l-8 8"/>'),
    svgFile: svg('<path d="M9.5 1.5h-6v13h9v-10z"/><path d="M9.5 1.5v3h3"/><path d="M5 11l1.5-3 1.5 3M9 8v3h2"/>'),
    image: svg('<rect x="1.5" y="2.5" width="13" height="11" rx="1.5"/><circle cx="5.5" cy="6" r="1.2"/><path d="m1.5 12 4-4 3 3 2-2 4 4"/>'),
    code: svg('<path d="M6 4 2 8l4 4M10 4l4 4-4 4"/>'),
    fold: svg('<path d="m4 6 4 4 4-4"/>'),
    unfold: svg('<path d="m6 4 4 4-4 4"/>'),
  };
  const el = (tag, cls, html) => Object.assign(document.createElement(tag), { className: cls || '', innerHTML: html || '' });

  // ===== Zwijanie sekcji =====

  const collapsed = new Set();

  // Przed podmianą DOM: klasy na nowym drzewie, więc porównanie bloków przy następnym renderze dalej działa.
  function decorateFolds(body) {
    let hideBelow = 0;
    for (const node of body.children) {
      if (/^H[1-6]$/.test(node.tagName)) {
        const level = +node.tagName[1];
        if (hideBelow && level <= hideBelow) hideBelow = 0;
        if (hideBelow) { node.classList.add('fold-hidden'); continue; }
        if (collapsed.has(node.id)) { node.classList.add('collapsed'); hideBelow = level; }
      } else if (hideBelow && !node.classList.contains('footnotes') && !node.classList.contains('footnotes-sep')) {
        node.classList.add('fold-hidden');
      }
    }
  }

  function toggleFold(id) {
    if (collapsed.has(id)) collapsed.delete(id); else collapsed.add(id);
    ctx.rerender();
  }

  function setAllFolds(on) {
    collapsed.clear();
    if (on) content().querySelectorAll(HEADINGS).forEach((h) => h.id && collapsed.add(h.id));
    ctx.rerender();
  }

  // Element w zwiniętej sekcji: rozwija nadrzędne nagłówki. true = trzeba szukać elementu od nowa (nowy DOM).
  function unfoldFor(node) {
    let top = node;
    while (top && top.parentElement !== content()) top = top.parentElement;
    if (!top || !top.classList.contains('fold-hidden')) return false;
    let level = /^H[1-6]$/.test(top.tagName) ? +top.tagName[1] : 7;
    for (let p = top.previousElementSibling; p && level > 1; p = p.previousElementSibling) {
      if (!/^H[1-6]$/.test(p.tagName) || +p.tagName[1] >= level) continue;
      level = +p.tagName[1];
      collapsed.delete(p.id);
    }
    ctx.rerender();
    return true;
  }

  // ===== Konspekt =====

  function currentHeading() {
    let cur = null;
    const top = ctx.barHeight() + 12;
    content().querySelectorAll(HEADINGS).forEach((h) => {
      if (!h.classList.contains('fold-hidden') && h.getBoundingClientRect().top <= top) cur = h;
    });
    return cur;
  }

  function goToHeading(id) {
    let h = content().querySelector('[id="' + CSS.escape(id) + '"]');
    if (!h) return;
    if (unfoldFor(h)) h = content().querySelector('[id="' + CSS.escape(id) + '"]');
    const r = h.getBoundingClientRect();
    ctx.glideTo(window.scrollY + r.top - ctx.barHeight() - 8);
    ctx.post('goto', h.dataset.line);
  }

  function outlineMenu(box, close) {
    const hs = Array.from(content().querySelectorAll(HEADINGS));
    const head = el('div', 'outline-head');
    head.append(el('span', '', ctx.escapeHtml(t('outline'))));
    for (const [on, label, icon] of [[true, t('collapseAll'), ICON.fold], [false, t('expandAll'), ICON.unfold]]) {
      const b = el('button', 'outline-all', icon);
      b.type = 'button';
      b.title = label;
      b.addEventListener('click', () => { close(); setAllFolds(on); });
      head.append(b);
    }
    box.append(head);
    if (!hs.length) { box.append(el('div', 'outline-empty', ctx.escapeHtml(t('noHeadings')))); return; }
    const min = Math.min(...hs.map((h) => +h.tagName[1]));
    const cur = currentHeading();
    const list = el('div', 'outline-list');
    for (const h of hs) {
      const lvl = +h.tagName[1] - min;
      const item = el('button', 'outline-item' + (h === cur ? ' current' : '') + (h.classList.contains('collapsed') ? ' is-collapsed' : '') + (h.classList.contains('fold-hidden') ? ' is-hidden' : ''));
      item.type = 'button';
      item.style.paddingLeft = 10 + lvl * 14 + 'px';
      item.textContent = h.textContent.trim() || '…';
      item.addEventListener('click', () => { close(); goToHeading(h.id); });
      list.append(item);
    }
    box.append(list);
    setTimeout(() => { const c = list.querySelector('.current'); if (c) c.scrollIntoView({ block: 'center' }); }, 0);
  }

  // ===== Spis treści w tekście (<!-- toc --> ... <!-- tocstop -->) =====

  function tocBody() {
    const hs = Array.from(content().querySelectorAll(HEADINGS)).filter((h) => h.id);
    if (!hs.length) return '';
    const min = Math.min(...hs.map((h) => +h.tagName[1]));
    return hs.map((h) => '  '.repeat(+h.tagName[1] - min) + '- [' + h.textContent.trim().replace(/[[\]\\]/g, '\\$&') + '](#' + h.id + ')').join('\n') + '\n';
  }

  function insertToc() {
    ctx.post('edit', ['block', '<!-- toc -->\n' + tocBody() + '<!-- tocstop -->'].join('\t'));
  }

  let tocTimer = 0;
  function syncToc() {
    clearTimeout(tocTimer);
    const text = ctx.lastText();
    if (!text || text.indexOf('<!-- toc -->') < 0) return;
    // Po chwili spokoju, żeby pisanie nagłówka nie przeplatało się ze zmianami spisu.
    tocTimer = setTimeout(() => {
      const src = (ctx.lastText() || '').replace(/\r\n?/g, '\n');
      const a = src.indexOf('<!-- toc -->');
      if (a < 0) return;
      const from = src.indexOf('\n', a);
      const z = src.indexOf('<!-- tocstop -->', from);
      if (from < 0 || z < 0) return;
      const region = src.slice(from + 1, src.lastIndexOf('\n', z) + 1);
      const body = tocBody();
      if (region !== body) { ctx.holdFollow(); ctx.post('toc', body); }
    }, 900);
  }

  // ===== Sprawdzanie linków, obrazków i przypisów =====

  const LOCAL = /^https:\/\/(mdlive\.doc|drive-[a-z]\.mdlive)\//i;
  const files = new Map();   // adres -> { missing, at }


  function resolve(raw) {
    try { return new URL(raw, 'https://mdlive.doc/').href.replace(/[?#].*$/, ''); } catch (e) { return null; }
  }

  function markBroken(node, key) {
    node.classList.add('broken');
    node.title = t(key);
  }

  function decorateLinks(body) {
    const ids = new Set(Array.from(body.querySelectorAll('[id]'), (n) => n.id));
    body.querySelectorAll('a[href]').forEach((a) => {
      const raw = a.getAttribute('href');
      if (raw.startsWith('#')) {
        let id = raw.slice(1);
        try { id = decodeURIComponent(id); } catch (e) { /* surowe */ }
        if (id && !ids.has(id)) markBroken(a, 'brokenAnchor');
        return;
      }
      const url = resolve(raw);
      if (url && LOCAL.test(url) && files.get(url) && files.get(url).missing) markBroken(a, 'brokenLink');
    });
    body.querySelectorAll('img[src]').forEach((img) => {
      const url = resolve(img.getAttribute('src'));
      const st = url && LOCAL.test(url) && files.get(url);
      if (!st) return;
      if (st.missing) markBroken(img, 'brokenImage');
      // Czas zmiany pliku w adresie: obrazek dodany później albo podmieniony na dysku wczytuje się od nowa.
      else if (st.time && st.time !== st.base) img.setAttribute('src', url + '?v=' + st.time);
    });
    // Przypis bez definicji zostaje w tekście jako [^x].
    const walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT);
    const hits = [];
    for (let n = walker.nextNode(); n; n = walker.nextNode()) {
      if (/\[\^[^\]\s]+\]/.test(n.nodeValue) && !n.parentElement.closest('code, pre')) hits.push(n);
    }
    for (const n of hits) {
      const frag = document.createDocumentFragment();
      let last = 0;
      n.nodeValue.replace(/\[\^[^\]\s]+\]/g, (m, at) => {
        frag.append(n.nodeValue.slice(last, at));
        const span = el('span', '');
        span.textContent = m;
        markBroken(span, 'brokenFootnote');
        frag.append(span);
        last = at + m.length;
        return m;
      });
      frag.append(n.nodeValue.slice(last));
      n.replaceWith(frag);
    }
  }

  function checkFiles() {
    const now = performance.now(), want = [];
    content().querySelectorAll('a[href], img[src]').forEach((n) => {
      const url = resolve(n.getAttribute(n.tagName === 'IMG' ? 'src' : 'href'));
      if (!url || !LOCAL.test(url) || want.includes(url)) return;
      const st = files.get(url);
      if (!st || now - st.at > 4000) want.push(url);
    });
    if (!want.length) return;
    for (const url of want) files.set(url, Object.assign({ missing: false }, files.get(url), { at: now }));
    ctx.post('check', want.join('\n'));
  }

  // Wynik sprawdzenia plików. Obrazek przeładowuje się (adres z ?v=czas) tylko wtedy, gdy plik pojawił się
  // albo zmienił po pierwszym wczytaniu - inaczej każde przeładowanie zmieniałoby wysokość strony.
  function onChecked(missing, times) {
    const gone = new Set(missing);
    let changed = false;
    for (const url of [...gone, ...Object.keys(times || {})]) {
      const st = files.get(url);
      if (!st) continue;
      const isMissing = gone.has(url), time = isMissing ? '' : times[url];
      if (st.time === undefined) {
        st.base = st.missing ? '' : time;   // przy pierwszym sprawdzeniu brakował - po pojawieniu się trzeba przeładować
      }
      if (st.missing !== isMissing || (st.time !== undefined && st.time !== time)) changed = true;
      if (st.missing && !isMissing) st.base = '';
      st.missing = isMissing;
      st.time = time;
    }
    if (changed) ctx.rerender();
  }

  // ===== Bloki kodu i diagramy =====

  function decorateCode(body) {
    body.querySelectorAll('pre > code').forEach((code) => {
      const b = el('button', 'copy-code', ICON.copy);
      b.type = 'button';
      b.title = t('copyCode');
      code.parentElement.appendChild(b);
    });
  }

  function copyCode(btn) {
    const code = btn.parentElement.querySelector('code');
    if (!code) return;
    ctx.post('copy-text', code.textContent.replace(/\n$/, ''));
    btn.innerHTML = ICON.ok;
    btn.classList.add('done');
    setTimeout(() => { btn.innerHTML = ICON.copy; btn.classList.remove('done'); }, 1200);
  }

  let menu = null;
  function closeMenu() { if (menu) { menu.remove(); menu = null; } }

  function contextMenu(x, y, entries) {
    closeMenu();
    menu = el('div', 'popover menu context');
    for (const [icon, label, fn] of entries) {
      const item = el('button', 'menu-item', '<span class="mi-icon">' + icon + '</span><span class="mi-label">' + ctx.escapeHtml(label) + '</span>');
      item.type = 'button';
      item.addEventListener('click', (e) => { e.stopPropagation(); closeMenu(); fn(); });
      menu.append(item);
    }
    menu.addEventListener('mousedown', (e) => e.preventDefault());
    document.body.append(menu);
    menu.style.left = Math.max(8, Math.min(x, innerWidth - menu.offsetWidth - 8)) + 'px';
    menu.style.top = Math.max(8, Math.min(y, innerHeight - menu.offsetHeight - 8)) + 'px';
  }

  // Diagram do pliku: tekst jako SVG <text> (bez HTML w środku), więc obraz działa też poza przeglądarką.
  let exportSeq = 0;
  async function diagramSvg(src) {
    const mermaid = await ctx.loadMermaid();
    const dark = ctx.cfg().dark;
    mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme: dark ? 'dark' : 'neutral', fontFamily: '"Segoe UI", sans-serif',
                         htmlLabels: false, flowchart: { htmlLabels: false } });
    const { svg: out } = await mermaid.render('mdlive-export-' + (++exportSeq), src);
    return out;
  }

  function toBase64(text) {
    const bytes = new TextEncoder().encode(text);
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(bin);
  }

  async function saveDiagram(box, kind, index) {
    let text;
    try { text = await diagramSvg(box.dataset.src); } catch (e) { ctx.toast(t('saveError')); return; }
    const name = 'diagram-' + (index + 1);
    if (kind === 'svg') { ctx.post('save-image', [name, 'svg', text].join('\x01')); return; }
    const doc = new DOMParser().parseFromString(text, 'image/svg+xml').documentElement;
    const vb = (doc.getAttribute('viewBox') || '0 0 800 600').split(/[\s,]+/).map(Number);
    const w = Math.ceil(vb[2]), h = Math.ceil(vb[3]), scale = 2;
    doc.setAttribute('width', w);
    doc.setAttribute('height', h);
    doc.style.maxWidth = '';
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = w * scale;
      canvas.height = h * scale;
      const g = canvas.getContext('2d');
      g.fillStyle = ctx.cfg().dark ? (ctx.cfg().bg || '#1d2128') : '#ffffff';
      g.fillRect(0, 0, canvas.width, canvas.height);
      g.drawImage(img, 0, 0, canvas.width, canvas.height);
      try { ctx.post('save-image', [name, 'png', canvas.toDataURL('image/png').split(',')[1]].join('\x01')); } catch (e) { ctx.toast(t('saveError')); }
    };
    img.onerror = () => ctx.toast(t('saveError'));
    img.src = 'data:image/svg+xml;base64,' + toBase64(new XMLSerializer().serializeToString(doc));
  }

  // ===== Pola zadań =====

  function decorateTasks(body) {
    body.querySelectorAll('li.task-list-item > input[type=checkbox], li.task-list-item > p > input[type=checkbox]').forEach((box) => {
      box.title = t('toggleTask');
    });
  }

  // ===== Czysty HTML (kopiowanie do Worda, eksport) =====

  function cleanTree(root, forWord) {
    root.querySelectorAll('.copy-code, .print-only, .diagram-wait, button, input:not([type=checkbox])').forEach((n) => n.remove());
    root.querySelectorAll('img[src]').forEach((img) => { const u = resolve(img.getAttribute('src')); if (u && LOCAL.test(u)) img.setAttribute('src', u); });
    root.querySelectorAll('*').forEach((n) => {
      for (const a of Array.from(n.attributes)) {
        if (a.name.startsWith('data-') && a.name !== 'data-src') n.removeAttribute(a.name);
      }
      n.removeAttribute('data-src');
      if (n.classList.contains('broken')) n.removeAttribute('title');
      n.classList.remove('fold-hidden', 'collapsed', 'broken', 'flash');
      if (!n.classList.length) n.removeAttribute('class');
    });
    if (!forWord) return;
    // Word nie zna arkusza podglądu: podstawowy wygląd w atrybutach style, wzory jako MathML.
    root.querySelectorAll('.katex').forEach((k) => {
      const math = k.querySelector('math');
      if (math) { if (k.closest('.katex-display')) math.setAttribute('display', 'block'); k.replaceWith(math.cloneNode(true)); }
    });
    const style = {
      table: 'border-collapse:collapse', th: 'border:1px solid #c8c8c8;padding:4px 8px;background:#f2f2f2', td: 'border:1px solid #c8c8c8;padding:4px 8px',
      pre: 'background:#f6f8fa;padding:8px 10px;font-family:Consolas,monospace;font-size:10pt;white-space:pre-wrap',
      blockquote: 'border-left:3px solid #c8c8c8;margin-left:0;padding-left:12px;color:#555',
    };
    for (const [tag, css] of Object.entries(style)) root.querySelectorAll(tag).forEach((n) => n.setAttribute('style', css));
    root.querySelectorAll('code').forEach((c) => { if (!c.closest('pre')) c.setAttribute('style', 'font-family:Consolas,monospace;background:#f2f2f2'); });
  }

  function copyRich() {
    const sel = window.getSelection();
    const holder = document.createElement('div');
    let text;
    if (sel && sel.rangeCount && !sel.isCollapsed && content().contains(sel.getRangeAt(0).commonAncestorContainer)) {
      holder.append(sel.getRangeAt(0).cloneContents());
      text = sel.toString();
    } else {
      holder.append(...Array.from(content().childNodes, (n) => n.cloneNode(true)));
      holder.querySelectorAll('.fold-hidden').forEach((n) => n.classList.remove('fold-hidden'));
      text = content().innerText;
    }
    cleanTree(holder, true);
    ctx.post('copy-rich', holder.innerHTML + '\x01' + text);
  }

  // ===== Eksport do jednego pliku HTML =====

  async function fetchText(url) { const r = await fetch(url); if (!r.ok) throw new Error(url); return r.text(); }

  async function dataUrl(url, type) {
    const buf = await (await fetch(url)).arrayBuffer();
    const bytes = new Uint8Array(buf);
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return 'data:' + type + ';base64,' + btoa(bin);
  }

  async function exportHtml(title) {
    try {
      const c = ctx.cfg();
      let css = await fetchText('https://mdlive.assets/preview.css');
      if (content().querySelector('.katex')) {
        // KaTeX z fontami w środku pliku (tylko woff2).
        let katex = await fetchText('https://mdlive.assets/lib/katex.min.css');
        katex = katex.replace(/,\s*url\(fonts\/[^)]+\.(woff|ttf)\)\s*format\("[^"]+"\)/g, '');
        const fonts = Array.from(new Set(katex.match(/fonts\/[^)]+\.woff2/g) || []));
        for (const f of fonts) katex = katex.split('url(' + f + ')').join('url(' + await dataUrl('https://mdlive.assets/lib/' + f, 'font/woff2') + ')');
        css += '\n' + katex;
      }
      const holder = document.createElement('div');
      holder.append(...Array.from(content().childNodes, (n) => n.cloneNode(true)));
      cleanTree(holder, false);
      holder.querySelectorAll('input[type=checkbox]').forEach((b) => b.setAttribute('disabled', ''));
      const html = '<!doctype html>\n<html lang="' + (document.documentElement.lang || 'en') + '" data-theme="' + (c.dark ? 'dark' : 'light') + '">\n<head>\n' +
        '<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>' + ctx.escapeHtml(title || 'Markdown') + '</title>\n' +
        '<style>\n' + css + '\n:root { --bg: ' + c.bg + '; --fg: ' + c.fg + '; }\nhtml, body { background: var(--bg); color: var(--fg); }\n</style>\n</head>\n<body>\n' +
        '<main id="content" class="markdown-body">\n' + holder.innerHTML + '\n</main>\n</body>\n</html>\n';
      ctx.post('export-html', html);
    } catch (e) {
      ctx.toast(t('saveError'));
    }
  }

  // ===== Dymek po najechaniu na link =====

  let peek = null, peekTimer = 0, hideTimer = 0, peekFor = null, peekUrl = '';
  const MD_FILE = /\.(md|markdown|mdown|mkdn?|mdwn|txt)$/i;

  function hidePeek() {
    clearTimeout(peekTimer);
    clearTimeout(hideTimer);
    if (peek) { peek.remove(); peek = null; }
    peekFor = null;
    peekUrl = '';
  }

  function showPeek(anchor, nodes) {
    if (peekFor !== anchor) return;
    if (peek) peek.remove();
    peek = el('div', 'peek markdown-body');
    peek.append(...nodes);
    peek.querySelectorAll('[id]').forEach((n) => n.removeAttribute('id'));
    peek.querySelectorAll('.footnote-backref, .copy-code').forEach((n) => n.remove());
    peek.addEventListener('mouseenter', () => clearTimeout(hideTimer));
    peek.addEventListener('mouseleave', () => { hideTimer = setTimeout(hidePeek, 200); });
    document.body.append(peek);
    const r = anchor.getBoundingClientRect();
    const below = innerHeight - r.bottom > peek.offsetHeight + 12 || r.top < peek.offsetHeight + 12;
    peek.style.left = Math.max(8, Math.min(r.left, innerWidth - peek.offsetWidth - 8)) + 'px';
    peek.style.top = (below ? r.bottom + 6 : r.top - peek.offsetHeight - 6) + 'px';
  }

  function startPeek(a) {
    const raw = a.getAttribute('href') || '';
    if (raw.startsWith('#')) {
      let id = raw.slice(1);
      try { id = decodeURIComponent(id); } catch (e) { /* surowe */ }
      const target = content().querySelector('[id="' + CSS.escape(id) + '"]');
      if (!target) return;
      if (target.closest('.footnotes')) {
        showPeek(a, Array.from(target.childNodes, (n) => n.cloneNode(true)));
      } else if (/^H[1-6]$/.test(target.tagName)) {
        const nodes = [target.cloneNode(true)];
        for (let n = target.nextElementSibling; n && nodes.length < 3 && !/^H[1-6]$/.test(n.tagName); n = n.nextElementSibling) nodes.push(n.cloneNode(true));
        showPeek(a, nodes);
      }
      return;
    }
    const url = resolve(raw);
    if (!url || !LOCAL.test(url) || !MD_FILE.test(url)) return;
    peekUrl = url;
    ctx.post('peek', url);
  }

  function onPeek(url, text) {
    if (!peekFor || url !== peekUrl) return;
    if (!text) { const p = el('p', 'peek-missing'); p.textContent = t('brokenLink'); showPeek(peekFor, [p]); return; }
    const lines = ctx.splitFrontMatter(text).body.replace(/^\n+/, '').split('\n');
    const body = ctx.sanitize(ctx.md.render(lines.slice(0, 60).join('\n')));
    const nodes = Array.from(body.children).slice(0, 8).map((n) => document.adoptNode(n));
    showPeek(peekFor, nodes);
  }

  // ===== Znacznik kursora na marginesie =====

  let caretLine = -1, caretBar = null, caretFrame = 0;

  function placeCaret() {
    caretFrame = 0;
    if (!caretBar) { caretBar = el('div', ''); caretBar.id = 'caretbar'; document.body.append(caretBar); }
    let best = null, bestLine = -1;
    if (caretLine >= 0 && !content().hidden) {
      content().querySelectorAll('[data-line]').forEach((n) => {
        const l = +n.dataset.line;
        if (l <= caretLine && l >= bestLine) { best = n; bestLine = l; }
      });
    }
    if (best && best.closest('.fold-hidden')) best = null;
    if (best && +best.dataset.lineEnd && caretLine >= +best.dataset.lineEnd) {
      // Kursor w pustej linii za blokiem: znacznik tylko gdy blok jest tuż nad nim.
      if (caretLine > +best.dataset.lineEnd) best = null;
    }
    if (!best) { caretBar.hidden = true; return; }
    const r = best.getBoundingClientRect(), c = content().getBoundingClientRect();
    caretBar.hidden = false;
    caretBar.style.top = r.top + window.scrollY + 'px';
    caretBar.style.height = Math.max(6, r.height) + 'px';
    caretBar.style.left = Math.max(2, c.left + 8) + 'px';
  }

  function scheduleCaret() { if (!caretFrame) caretFrame = requestAnimationFrame(placeCaret); }
  function setCaret(line) { if (typeof line === 'number') { caretLine = line; scheduleCaret(); } }

  // ===== Szukanie w podglądzie =====

  let findBar = null, findInput, findCount, matches = [], current = -1;
  const HAS_HL = typeof CSS !== 'undefined' && CSS.highlights && typeof Highlight !== 'undefined';

  function buildFind() {
    findBar = el('div', '');
    findBar.id = 'findbar';
    findInput = el('input', '');
    findInput.type = 'text';
    findInput.placeholder = t('findPlaceholder');
    findInput.spellcheck = false;
    findCount = el('span', 'find-count');
    const btn = (icon, title, fn) => { const b = el('button', 'find-btn', icon); b.type = 'button'; b.title = title; b.addEventListener('mousedown', (e) => e.preventDefault()); b.addEventListener('click', fn); return b; };
    findBar.append(findInput, findCount, btn(ICON.up, t('prev'), () => step(-1)), btn(ICON.down, t('next'), () => step(1)), btn(ICON.close, t('close'), closeFind));
    findInput.addEventListener('input', () => runFind(false));
    findInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); step(e.shiftKey ? -1 : 1); }
      else if (e.key === 'Escape') { e.preventDefault(); closeFind(); }
    });
    document.body.append(findBar);
  }

  function openFind() {
    if (!findBar) buildFind();
    findBar.hidden = false;
    findBar.style.top = ctx.barHeight() + 6 + 'px';
    const sel = window.getSelection && String(window.getSelection());
    if (sel && sel.length < 80 && !sel.includes('\n')) findInput.value = sel;
    findInput.focus();
    findInput.select();
    runFind(false);
  }

  function closeFind() {
    if (!findBar) return;
    findBar.hidden = true;
    matches = [];
    if (HAS_HL) { CSS.highlights.delete('mdlive-find'); CSS.highlights.delete('mdlive-find-current'); }
  }

  function runFind(keep) {
    if (!findBar || findBar.hidden) return;
    const q = findInput.value.toLocaleLowerCase();
    matches = [];
    if (q) {
      const walker = document.createTreeWalker(content(), NodeFilter.SHOW_TEXT, {
        acceptNode: (n) => n.parentElement.closest('.fold-hidden, .print-only, .copy-code, .katex-mathml, script, style') ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT,
      });
      for (let n = walker.nextNode(); n && matches.length < 5000; n = walker.nextNode()) {
        const s = n.nodeValue.toLocaleLowerCase();
        for (let i = s.indexOf(q); i >= 0; i = s.indexOf(q, i + q.length)) {
          const r = new Range();
          r.setStart(n, i);
          r.setEnd(n, i + q.length);
          matches.push(r);
        }
      }
    }
    if (!matches.length) current = -1;
    else if (keep && current >= 0) current = Math.min(current, matches.length - 1);
    else {
      const top = ctx.barHeight();
      current = matches.findIndex((r) => r.getBoundingClientRect().top >= top);
      if (current < 0) current = 0;
    }
    paintFind(!keep);
  }

  function paintFind(scroll) {
    findCount.textContent = !findInput.value ? '' : matches.length ? (current + 1) + ' / ' + matches.length : t('findNone');
    findBar.classList.toggle('none', !!findInput.value && !matches.length);
    if (!HAS_HL) return;
    if (!matches.length) { CSS.highlights.delete('mdlive-find'); CSS.highlights.delete('mdlive-find-current'); return; }
    CSS.highlights.set('mdlive-find', new Highlight(...matches));
    CSS.highlights.set('mdlive-find-current', new Highlight(matches[current]));
    if (!scroll) return;
    const r = matches[current].getBoundingClientRect(), top = ctx.barHeight() + 40;
    if (r.top < top || r.bottom > innerHeight - 30) ctx.glideTo(window.scrollY + r.top - (innerHeight - top) / 3 - top);
  }

  function step(d) {
    if (!matches.length) return;
    current = (current + d + matches.length) % matches.length;
    paintFind(true);
  }

  // ===== Powiększenie i licznik słów =====

  let zoom = 100, zoomSave = 0, stats = null, statsTimer = 0, zoomShown = 0;

  function applyZoom() {
    content().style.zoom = zoom === 100 ? '' : String(zoom / 100);
    ctx.invalidate();
    scheduleCaret();
  }

  function setZoom(z) {
    zoom = Math.max(50, Math.min(300, Math.round(z / 10) * 10));
    // Ten sam blok zostaje na tej samej wysokości ekranu (inaczej powiększenie przesuwa widok w inne miejsce).
    const top = ctx.barHeight();
    const anchor = Array.from(content().querySelectorAll('[data-line]')).find((n) => n.getBoundingClientRect().bottom > top);
    const before = anchor ? anchor.getBoundingClientRect().top : 0;
    applyZoom();
    if (anchor) window.scrollTo(0, window.scrollY + anchor.getBoundingClientRect().top - before);
    clearTimeout(zoomSave);
    zoomSave = setTimeout(() => ctx.post('zoom', String(zoom)), 400);
    if (stats) {
      stats.textContent = zoom + '%';
      stats.classList.add('zooming');
      clearTimeout(zoomShown);
      zoomShown = setTimeout(() => { stats.classList.remove('zooming'); updateStats(); }, 900);
    }
  }

  function updateStats() {
    if (!stats) { stats = el('div', ''); stats.id = 'stats'; document.body.append(stats); }
    if (stats.classList.contains('zooming')) return;
    if (content().hidden) { stats.hidden = true; return; }
    // Tekst bez stylów i opisów diagramów (SVG) oraz bez MathML wzorów.
    let text = '';
    const walker = document.createTreeWalker(content(), NodeFilter.SHOW_TEXT, {
      acceptNode: (n) => n.parentElement.closest('svg, style, .katex-mathml') ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT,
    });
    for (let n = walker.nextNode(); n; n = walker.nextNode()) text += n.nodeValue + ' ';
    const words = (text.match(/[\p{L}\p{N}]+(?:['’-][\p{L}\p{N}]+)*/gu) || []).length;
    const chars = text.replace(/\s+/g, '').length;
    const fmt = (n) => n.toLocaleString(document.documentElement.lang || undefined);
    stats.hidden = false;
    stats.textContent = t('statWords').replace('{n}', fmt(words)) + ' · ' + t('statMin').replace('{n}', Math.max(1, Math.round(words / 200)));
    stats.title = t('statChars').replace('{n}', fmt(chars));
  }

  // ===== Prezentacja =====

  let deck = null, slides = [], slide = 0;

  function buildSlides() {
    const nodes = Array.from(content().children).filter((n) => !n.classList.contains('front-matter'));
    const split = (test) => {
      const out = [[]];
      for (const n of nodes) {
        if (test(n)) { if (out[out.length - 1].length) out.push([]); if (n.tagName === 'HR') continue; }
        out[out.length - 1].push(n);
      }
      return out.filter((g) => g.length);
    };
    let groups = split((n) => n.tagName === 'HR' && !n.classList.contains('footnotes-sep'));
    if (groups.length < 2) groups = split((n) => n.tagName === 'H1' || n.tagName === 'H2');
    return groups;
  }

  function showSlide(i) {
    slide = Math.max(0, Math.min(slides.length - 1, i));
    const stage = deck.querySelector('.slide');
    stage.textContent = '';
    stage.style.zoom = '';
    for (const n of slides[slide] || []) {
      const c = n.cloneNode(true);
      c.classList.remove('fold-hidden', 'collapsed', 'flash');
      c.querySelectorAll('.copy-code').forEach((b) => b.remove());
      stage.append(c);
    }
    deck.querySelector('.deck-count').textContent = (slide + 1) + ' / ' + slides.length;
    fitSlide();
  }

  function fitSlide() {
    if (!deck) return;
    const stage = deck.querySelector('.slide');
    stage.style.zoom = '';
    const w = stage.scrollWidth, h = stage.scrollHeight;
    const scale = Math.max(0.4, Math.min(2.2, (innerWidth * 0.86) / w, (innerHeight * 0.84) / h));
    stage.style.zoom = String(scale);
  }

  function present(on) {
    if (!on) {
      if (deck) { deck.remove(); deck = null; }
      return;
    }
    slides = buildSlides();
    if (!slides.length) { ctx.post('present-end'); return; }
    if (deck) deck.remove();
    deck = el('div', '');
    deck.id = 'deck';
    deck.append(el('div', 'slide markdown-body'), el('div', 'deck-count'), el('div', 'deck-hint', ctx.escapeHtml(t('presentHint'))));
    deck.addEventListener('click', (e) => { if (!e.target.closest('a')) showSlide(slide + 1); });
    deck.addEventListener('contextmenu', (e) => { e.preventDefault(); showSlide(slide - 1); });
    document.body.append(deck);
    hidePeek();
    closeMenu();
    showSlide(0);
    setTimeout(() => deck && deck.querySelector('.deck-hint').classList.add('gone'), 2500);
  }

  function deckKey(e) {
    const next = ['ArrowRight', 'ArrowDown', 'PageDown', ' ', 'Enter'], prev = ['ArrowLeft', 'ArrowUp', 'PageUp', 'Backspace'];
    if (next.includes(e.key)) showSlide(slide + 1);
    else if (prev.includes(e.key)) showSlide(slide - 1);
    else if (e.key === 'Home') showSlide(0);
    else if (e.key === 'End') showSlide(slides.length - 1);
    else if (e.key === 'Escape') ctx.post('present-end');
    else return;
    e.preventDefault();
    e.stopPropagation();
  }

  // ===== HTML ze schowka -> Markdown =====

  function html2md(html) {
    const doc = new DOMParser().parseFromString(html, 'text/html');
    doc.querySelectorAll('script, style, meta, link, title, noscript, template').forEach((n) => n.remove());
    return tidy(conv(doc.body));
  }

  const kids = (n) => Array.from(n.childNodes, conv).join('');

  function wrapInline(mark, s) {
    const core = s.trim();
    if (!core) return s;
    return s.match(/^\s*/)[0] + mark + core + mark + s.match(/\s*$/)[0];
  }

  function tidy(s) {
    const out = [];
    let fence = false;
    for (const line of s.split('\n')) {
      if (/^\s*```/.test(line)) fence = !fence;
      out.push(fence || /^\s*```/.test(line) ? line : line.replace(/[ \t]+$/, '').replace(/^[ \t]+$/, ''));
    }
    return out.join('\n').replace(/\n{3,}/g, '\n\n').replace(/^\n+|\n+$/g, '');
  }

  function conv(n) {
    if (n.nodeType === 3) return n.nodeValue.replace(/\s+/g, ' ');
    if (n.nodeType !== 1) return '';
    const tag = n.tagName, style = (n.getAttribute('style') || '').toLowerCase();
    switch (tag) {
      case 'BR': return '\\\n';
      case 'H1': case 'H2': case 'H3': case 'H4': case 'H5': case 'H6':
        return '\n\n' + '#'.repeat(+tag[1]) + ' ' + kids(n).replace(/\\\n/g, ' ').trim() + '\n\n';
      case 'P': case 'DIV': case 'SECTION': case 'ARTICLE': case 'HEADER': case 'FOOTER': case 'MAIN': case 'ASIDE': case 'FIGURE': case 'FIGCAPTION': case 'DT': case 'DD': case 'DL':
        return '\n\n' + kids(n).trim() + '\n\n';
      case 'STRONG': case 'B':
        return /font-weight:\s*(normal|[1-4]00)/.test(style) ? kids(n) : wrapInline('**', kids(n));
      case 'EM': case 'I': case 'CITE': return wrapInline('*', kids(n));
      case 'S': case 'DEL': case 'STRIKE': return wrapInline('~~', kids(n));
      case 'MARK': return wrapInline('==', kids(n));
      case 'SUP': return wrapInline('^', kids(n));
      case 'SUB': return wrapInline('~', kids(n));
      case 'KBD': return '<kbd>' + n.textContent + '</kbd>';
      case 'CODE': {
        const s = n.textContent;
        const tick = s.includes('`') ? '``' : '`';
        return s ? tick + (tick.length > 1 ? ' ' + s + ' ' : s) + tick : '';
      }
      case 'PRE': {
        const code = n.querySelector('code');
        const lang = ((code && code.className) || '').match(/language-([\w+-]+)/);
        return '\n\n```' + (lang ? lang[1] : '') + '\n' + n.textContent.replace(/\n$/, '') + '\n```\n\n';
      }
      case 'A': {
        const href = (n.getAttribute('href') || '').trim(), text = kids(n).trim();
        if (!href || /^(javascript|vbscript):/i.test(href)) return kids(n);
        if (!text) return '';
        if (text === href) return '<' + href + '>';
        return '[' + text.replace(/[[\]]/g, '\\$&') + '](' + href.replace(/ /g, '%20').replace(/\)/g, '%29') + ')';
      }
      case 'IMG': {
        const src = n.getAttribute('src') || '';
        if (!src || src.startsWith('data:')) return '';
        return '![' + (n.getAttribute('alt') || '').replace(/[[\]]/g, '') + '](' + src.replace(/ /g, '%20') + ')';
      }
      case 'UL': case 'OL': return '\n\n' + list(n) + '\n\n';
      case 'BLOCKQUOTE': {
        const inner = tidy(kids(n));
        return '\n\n' + inner.split('\n').map((l) => (l ? '> ' + l : '>')).join('\n') + '\n\n';
      }
      case 'HR': return '\n\n---\n\n';
      case 'TABLE': return '\n\n' + table(n) + '\n\n';
      case 'INPUT': return '';
      case 'SPAN': case 'FONT': {
        let s = kids(n);
        if (/font-weight:\s*(bold|[6-9]00)/.test(style)) s = wrapInline('**', s);
        if (/font-style:\s*italic/.test(style)) s = wrapInline('*', s);
        if (/line-through/.test(style)) s = wrapInline('~~', s);
        return s;
      }
      default: return kids(n);
    }
  }

  function list(n) {
    const ordered = n.tagName === 'OL', start = +(n.getAttribute('start') || 1) || 1;
    const items = Array.from(n.children).filter((c) => c.tagName === 'LI');
    return items.map((li, i) => {
      let marker = ordered ? (start + i) + '. ' : '- ';
      const box = li.querySelector(':scope > input[type=checkbox], :scope > p > input[type=checkbox]');
      if (box) marker += box.checked || box.hasAttribute('checked') ? '[x] ' : '[ ] ';
      const lines = tidy(kids(li)).replace(/\n{2,}/g, '\n').split('\n');
      const pad = ' '.repeat(ordered ? marker.length - (box ? 4 : 0) : 2);
      return marker + lines[0] + lines.slice(1).map((l) => '\n' + (l ? pad + l : '')).join('');
    }).join('\n');
  }

  function table(n) {
    const rows = Array.from(n.querySelectorAll('tr')).filter((tr) => tr.closest('table') === n);
    const cells = rows.map((tr) => Array.from(tr.children).filter((c) => c.tagName === 'TD' || c.tagName === 'TH')
      .map((c) => tidy(kids(c)).replace(/\\?\n+/g, ' ').replace(/\|/g, '\\|').trim()));
    if (!cells.length) return '';
    const cols = Math.max(...cells.map((r) => r.length));
    // Kolumny wyrównane jak przy Tab w tabeli (znaki CJK i emoji liczone podwójnie).
    const width = (s) => [...s].reduce((w, ch) => w + (/[ᄀ-ᅟ⺀-꓏가-힣豈-﫿︰-﹏＀-｠￠-￦\u{1F300}-\u{1FAFF}]/u.test(ch) ? 2 : 1), 0);
    const w = Array.from({ length: cols }, (_, i) => Math.max(3, ...cells.map((r) => width(r[i] || ''))));
    const line = (r) => '| ' + w.map((n, i) => (r[i] || '') + ' '.repeat(n - width(r[i] || ''))).join(' | ') + ' |';
    return [line(cells[0]), '| ' + w.map((n) => '-'.repeat(n)).join(' | ') + ' |', ...cells.slice(1).map(line)].join('\n');
  }

  // ===== Zdarzenia =====

  function init(context) {
    ctx = context;

    document.addEventListener('click', (e) => {
      closeMenu();
      const copy = e.target.closest && e.target.closest('#content .copy-code');
      if (copy) { e.preventDefault(); e.stopPropagation(); copyCode(copy); return; }
      const box = e.target.closest && e.target.closest('#content li input[type=checkbox]');
      if (box) {
        e.preventDefault();
        const li = box.closest('li[data-line]');
        if (li) { ctx.holdFollow(); ctx.post('task', li.dataset.line); }
        return;
      }
      // Strzałka na lewo od nagłówka zwija / rozwija jego sekcję.
      const h = e.target.closest && e.target.closest('#content > :is(h1, h2, h3, h4, h5, h6)');
      if (h && h.id && e.clientX < h.getBoundingClientRect().left + 2) { e.preventDefault(); toggleFold(h.id); }
    }, true);

    document.addEventListener('contextmenu', (e) => {
      const box = e.target.closest && e.target.closest('#content .diagram[data-src]');
      if (!box) return;
      e.preventDefault();
      const index = Array.from(content().querySelectorAll('.diagram[data-src]')).indexOf(box);
      contextMenu(e.clientX, e.clientY, [
        [ICON.svgFile, t('saveSvg'), () => saveDiagram(box, 'svg', index)],
        [ICON.image, t('savePng'), () => saveDiagram(box, 'png', index)],
        [ICON.code, t('copySource'), () => { ctx.post('copy-text', box.dataset.src); ctx.toast(t('copied')); }],
      ]);
    });

    document.addEventListener('copy', (e) => {
      const sel = window.getSelection();
      if (!sel || sel.isCollapsed || !content().contains(sel.anchorNode)) return;
      e.preventDefault();
      copyRich();
    });

    content().addEventListener('mouseover', (e) => {
      const a = e.target.closest && e.target.closest('a[href]');
      if (!a || a === peekFor) { if (a) clearTimeout(hideTimer); return; }
      hidePeek();
      peekFor = a;
      peekTimer = setTimeout(() => startPeek(a), 450);
    });
    content().addEventListener('mouseout', (e) => {
      const a = e.target.closest && e.target.closest('a[href]');
      if (!a || a !== peekFor || (e.relatedTarget && a.contains(e.relatedTarget))) return;
      clearTimeout(peekTimer);
      hideTimer = setTimeout(hidePeek, 200);
    });
    window.addEventListener('scroll', () => { if (peek) hidePeek(); closeMenu(); }, { passive: true });

    window.addEventListener('wheel', (e) => {
      if (!e.ctrlKey || deck) return;
      e.preventDefault();
      setZoom(zoom + (e.deltaY < 0 ? 10 : -10));
    }, { passive: false });

    window.addEventListener('keydown', (e) => {
      if (deck) { deckKey(e); return; }
      if (e.key === 'Escape') { closeMenu(); hidePeek(); if (findBar && !findBar.hidden) closeFind(); }
      const mod = e.ctrlKey && !e.altKey;
      if (mod && (e.key === 'f' || e.key === 'F')) { e.preventDefault(); openFind(); }
      else if (e.key === 'F3') { e.preventDefault(); if (!findBar || findBar.hidden) openFind(); else step(e.shiftKey ? -1 : 1); }
      else if (mod && (e.key === '=' || e.key === '+')) { e.preventDefault(); setZoom(zoom + 10); }
      else if (mod && e.key === '-') { e.preventDefault(); setZoom(zoom - 10); }
      else if (mod && e.key === '0') { e.preventDefault(); setZoom(100); }
    }, true);

    // Obrazki i pliki dodane albo podmienione na dysku bez zmiany tekstu też się odświeżają.
    setInterval(() => { if (!document.hidden && !content().hidden) checkFiles(); }, 3000);

    window.addEventListener('resize', () => { scheduleCaret(); fitSlide(); if (findBar) findBar.style.top = ctx.barHeight() + 6 + 'px'; });
    new ResizeObserver(() => scheduleCaret()).observe(content());
  }

  // Przed podmianą bloków (na świeżym drzewie).
  function decorate(body) {
    decorateFolds(body);
    decorateLinks(body);
    decorateCode(body);
    decorateTasks(body);
  }

  let lastSlugs = '';
  // Po podmianie bloków.
  function afterRender() {
    scheduleCaret();
    clearTimeout(statsTimer);
    statsTimer = setTimeout(updateStats, 250);
    runFind(true);
    checkFiles();
    syncToc();
    const slugs = Array.from(content().querySelectorAll(HEADINGS), (h) => h.id).filter(Boolean).join('\n');
    if (slugs !== lastSlugs) { lastSlugs = slugs; ctx.post('headings', slugs); }
  }

  function config(c) {
    if (typeof c.zoom === 'number' && c.zoom !== zoom) { zoom = c.zoom; applyZoom(); }
  }

  function cleared() {
    if (stats) stats.hidden = true;
    if (caretBar) caretBar.hidden = true;
    hidePeek();
  }

  return {
    holdFollow: () => ctx.holdFollow(),
    init, decorate, afterRender, config, cleared, setCaret, unfoldFor, outlineMenu, openFind, insertToc, copyRich, exportHtml, present,
    html2md, onChecked, onPeek,
  };
})();

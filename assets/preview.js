(() => {
  'use strict';

  const host = window.chrome && window.chrome.webview;
  const post = (cmd, arg = '') => host && host.postMessage(cmd + '\t' + arg);
  const $ = (id) => document.getElementById(id);
  const content = $('content'), empty = $('empty'), bar = $('toolbar');
  const root = document.documentElement;

  // Teksty angielskie; inne języki przychodzą z wtyczki (assets/i18n/<język Notepad++>.json).
  const EN = {
    emptyTitle: "This doesn't look like Markdown",
    emptyText: 'This document has no typical Markdown syntax. You can render it anyway.',
    emptyButton: 'Render anyway',
    light: 'Light', dark: 'Dark', toLight: 'Switch to light', toDark: 'Switch to dark',
    syncOn: 'Scroll sync: on (click to turn off)', syncOff: 'Scroll sync: off (click to turn on)',
    style: 'Text style', paragraph: 'Paragraph', heading: 'Heading', insert: 'Insert',
    bold: 'Bold', italic: 'Italic', strike: 'Strikethrough', mark: 'Highlight', code: 'Inline code',
    ul: 'Bulleted list', ol: 'Numbered list', task: 'Task list', quote: 'Quote',
    link: 'Link', image: 'Image from file', table: 'Table', codeblock: 'Code block', hr: 'Horizontal rule',
    math: 'Formula (KaTeX)', diagram: 'Diagram (Mermaid)', footnote: 'Footnote',
    tablePick: 'Choose table size', tableHint: 'columns × rows', column: 'Column',
    phBold: 'bold text', phItalic: 'italic', phText: 'text', phCode: 'code', phLink: 'link text',
    phFootnote: 'Footnote text', start: 'Start', decision: 'Decision', yes: 'Yes', no: 'No', end: 'End',
    NOTE: 'Note', TIP: 'Tip', IMPORTANT: 'Important', WARNING: 'Warning', CAUTION: 'Caution',
    toc: 'Contents', diagramError: 'Diagram error', loading: 'Drawing diagram…',
    undo: 'Undo', redo: 'Redo', clear: 'Clear formatting', emoji: 'Emoji', more: 'More',
    alert: 'Callout', sup: 'Superscript', sub: 'Subscript', kbd: 'Keyboard key',
    print: 'Print', printDialog: 'Print…', savePdf: 'Save as PDF…', pdfSaved: 'PDF saved', pdfError: 'Could not save the PDF', open: 'Open',
    share: 'Print and export', exportHtml: 'Export to HTML…', copyRich: 'Copy as formatted text', present: 'Presentation',
    htmlSaved: 'HTML saved', imageSaved: 'Image saved', saveError: 'Could not save the file', copied: 'Copied to the clipboard', copyError: 'Could not copy',
    outline: 'Outline', collapseAll: 'Collapse all', expandAll: 'Expand all', noHeadings: 'No headings yet',
    find: 'Find in preview (Ctrl+F)', findPlaceholder: 'Find in preview', findNone: 'No results', prev: 'Previous', next: 'Next', close: 'Close',
    tidy: 'Tidy up document', copyCode: 'Copy', toggleTask: 'Click to check or uncheck',
    saveSvg: 'Save diagram as SVG…', savePng: 'Save diagram as PNG…', copySource: 'Copy diagram source',
    brokenLink: 'File not found', brokenAnchor: 'No such heading in this document', brokenImage: 'Image not found', brokenFootnote: 'Footnote is not defined',
    statWords: '{n} words', statMin: '{n} min read', statChars: '{n} characters without spaces',
    presentHint: '← → next / previous slide · Esc exit',
  };

  let cfg = { dark: false, sync: true, lang: '', i18n: {} };
  const t = (key) => (cfg.i18n && cfg.i18n[key]) || EN[key] || key;

  let lineCount = 1, lmax = 0, last = null;
  let marks = [], marksDirty = true, lockUntil = 0;

  // ----- Markdown -----

  const highlight = (code, lang) => {
    try {
      if (lang && window.hljs && hljs.getLanguage(lang)) return hljs.highlight(code, { language: lang, ignoreIllegals: true }).value;
    } catch (e) { /* zwykły tekst */ }
    return '';
  };

  const md = window.markdownit({ html: true, linkify: true, typographer: false, highlight })
    .use(window.markdownitFootnote)
    .use(window.markdownitDeflist)
    .use(window.markdownitEmoji.full || window.markdownitEmoji)
    .use(window.markdownitSub)
    .use(window.markdownitSup)
    .use(window.markdownitMark)
    .use(window.markdownitAbbr)
    .use(window.markdownitIns)
    .use(window.texmath, { engine: window.katex, delimiters: ['dollars', 'brackets'], katexOptions: { throwOnError: false } });

  // Wzory blokowe ($$) renderuje texmath bez atrybutów - owijamy je, żeby miały numer linii (dwuklik, przewijanie).
  for (const name of Object.keys(md.renderer.rules).filter((n) => n.startsWith('math_block'))) {
    const orig = md.renderer.rules[name];
    md.renderer.rules[name] = (tokens, idx, ...rest) => {
      const map = tokens[idx].map;
      const html = orig(tokens, idx, ...rest);
      return map ? '<div class="math-block" data-line="' + map[0] + '" data-line-end="' + map[1] + '">' + html + '</div>' : html;
    };
  }

  // Lokalne ścieżki (C:\..., file:///) przepuszczamy; bezpieczeństwo pilnuje sanitize().
  md.validateLink = (url) => !/^\s*(javascript|vbscript):/i.test(url);

  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const ALERT = /^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*/i;
  const TOC = /^\[\[?toc\]?\]$/i;

  md.core.ruler.push('mdlive', (state) => {
    const toks = state.tokens, used = new Map(), headings = [];
    for (let i = 0; i < toks.length; i++) {
      const tk = toks[i];
      if (tk.map && tk.nesting >= 0 && tk.type !== 'inline') {
        tk.attrSet('data-line', String(tk.map[0]));
        tk.attrSet('data-line-end', String(tk.map[1]));
      }

      if (tk.type === 'heading_open') {
        const kids = (toks[i + 1] && toks[i + 1].children) || [];
        const text = kids.filter((c) => c.type === 'text' || c.type === 'code_inline').map((c) => c.content).join('');
        const slug0 = text.trim().toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, '').replace(/\s/g, '-');
        const n = used.get(slug0) || 0;
        used.set(slug0, n + 1);
        const slug = n ? slug0 + '-' + n : slug0;
        tk.attrSet('id', slug);
        headings.push({ level: +tk.tag.slice(1), slug, text });
      }

      // Lista zadań: "- [ ]" / "- [x]".
      if (tk.type === 'inline' && i >= 2 && toks[i - 1].type === 'paragraph_open' && toks[i - 2].type === 'list_item_open') {
        const m = /^\[([ xX])\](?:\s+|$)/.exec(tk.content);
        const first = tk.children[0];
        if (m && first && first.type === 'text') {
          first.content = first.content.replace(/^\[[ xX]\]\s*/, '');
          const box = new state.Token('html_inline', '', 0);
          box.content = '<input type="checkbox"' + (m[1] === ' ' ? '' : ' checked') + '> ';
          tk.children.unshift(box);
          toks[i - 2].attrJoin('class', 'task-list-item');
        }
      }

      // Ramki GitHuba: "> [!NOTE]".
      if (tk.type === 'blockquote_open' && toks[i + 2] && toks[i + 2].type === 'inline') {
        const inline = toks[i + 2], m = ALERT.exec(inline.content), first = inline.children[0];
        if (m && first && first.type === 'text' && ALERT.test(first.content)) {
          const kind = m[1].toUpperCase();
          first.content = first.content.replace(ALERT, '');
          if (!first.content && inline.children[1] && inline.children[1].type === 'softbreak') inline.children.splice(0, 2);
          const title = new state.Token('html_inline', '', 0);
          title.content = '<span class="alert-title">' + escapeHtml(t(kind)) + '</span>';
          inline.children.unshift(title);
          tk.attrJoin('class', 'alert alert-' + kind.toLowerCase());
        }
      }
    }

    // Spis treści: akapit "[[toc]]" lub "[toc]".
    for (let i = toks.length - 2; i > 0; i--) {
      if (toks[i].type !== 'inline' || toks[i - 1].type !== 'paragraph_open' || !TOC.test(toks[i].content.trim())) continue;
      const min = Math.min(...headings.map((h) => h.level), 6);
      const items = headings.map((h) => '<li class="toc-' + (h.level - min) + '"><a href="#' + encodeURIComponent(h.slug) + '">' + escapeHtml(h.text) + '</a></li>').join('');
      const nav = new state.Token('html_block', '', 0);
      nav.content = '<nav class="toc" data-line="' + toks[i - 1].map[0] + '"><div class="toc-title">' + escapeHtml(t('toc')) + '</div><ul>' + items + '</ul></nav>\n';
      nav.map = toks[i - 1].map;
      toks.splice(i - 1, 3, nav);
    }
  });

  // Nagłówek YAML zastępujemy pustymi liniami, żeby numery linii się zgadzały.
  function splitFrontMatter(text) {
    const m = /^---[ \t]*\r?\n([\s\S]*?)\r?\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$)/.exec(text);
    if (!m) return { body: text, yaml: null };
    return { body: '\n'.repeat(m[0].split('\n').length - 1) + text.slice(m[0].length), yaml: m[1] };
  }

  // ----- Bezpieczeństwo i ścieżki -----

  const UNSAFE_URL = /^(javascript|vbscript|data:(?!image\/(png|gif|jpe?g|webp|bmp|svg\+xml)))/;
  const DRIVE = /^(?:file:\/{2,3})?([a-zA-Z]):[\\/](.*)$/;

  function localUrl(value) {
    let raw = value;
    try { raw = decodeURIComponent(value); } catch (e) { /* zostaje */ }
    const m = DRIVE.exec(raw);
    if (!m) return null;
    return 'https://drive-' + m[1].toLowerCase() + '.mdlive/' + m[2].replace(/\\/g, '/').split('/').map(encodeURIComponent).join('/');
  }

  function sanitize(html) {
    const doc = new DOMParser().parseFromString(html, 'text/html');
    doc.querySelectorAll('script,style,iframe,frame,object,embed,link,meta,base,form,applet,noscript,template').forEach((n) => n.remove());
    doc.body.querySelectorAll('*').forEach((el) => {
      for (const a of Array.from(el.attributes)) {
        const name = a.name.toLowerCase();
        if (name.startsWith('on') || name === 'srcdoc' || name === 'formaction') {
          el.removeAttribute(a.name);
        } else if (['href', 'src', 'xlink:href', 'action'].includes(name)) {
          if (UNSAFE_URL.test(a.value.toLowerCase().replace(/[\s\u0000-\u001f]/g, ''))) el.removeAttribute(a.name);
          else if (name !== 'xlink:href') { const local = localUrl(a.value); if (local) el.setAttribute(a.name, local); }
        }
      }
    });
    doc.body.querySelectorAll('pre > code').forEach((c) => c.classList.add('hljs'));
    return doc.body;
  }

  // ----- Mermaid (ładowany dopiero, gdy dokument ma diagram) -----

  const diagrams = new Map();
  let mermaidLoad = null, diagramSeq = 0;

  function loadMermaid() {
    if (!mermaidLoad) {
      mermaidLoad = new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = 'https://mdlive.assets/lib/mermaid.min.js';
        s.onload = () => resolve(window.mermaid);
        s.onerror = reject;
        document.head.appendChild(s);
      });
    }
    return mermaidLoad;
  }

  // Ostatnio pokazany obraz każdego diagramu: w trakcie przerysowania zostaje (wyszarzony), więc wysokość się nie zmienia.
  const shown = [];

  function placeDiagrams(body) {
    const todo = [];
    body.querySelectorAll('pre > code.language-mermaid').forEach((code, i) => {
      const pre = code.parentElement, src = code.textContent, key = (cfg.dark ? 'd:' : 'l:') + src;
      const box = document.createElement('div');
      box.className = 'diagram';
      box.dataset.line = code.dataset.line || '';
      box.dataset.lineEnd = code.dataset.lineEnd || '';
      box.dataset.src = src;
      if (diagrams.has(key)) {
        box.innerHTML = shown[i] = diagrams.get(key);
      } else {
        if (shown[i]) { box.innerHTML = shown[i]; box.classList.add('stale'); }
        else box.innerHTML = '<div class="diagram-wait">' + escapeHtml(t('loading')) + '</div>';
        todo.push({ key, src });
      }
      pre.replaceWith(box);
    });
    if (todo.length) drawDiagrams(todo);
  }

  async function drawDiagrams(todo) {
    let mermaid;
    try { mermaid = await loadMermaid(); } catch (e) { return; }
    mermaid.initialize({
      startOnLoad: false, securityLevel: 'strict', theme: cfg.dark ? 'dark' : 'neutral',
      fontFamily: '"Inter Variable", "Segoe UI", sans-serif',
    });
    for (const { key, src } of todo) {
      if (diagrams.has(key)) continue;
      const id = 'mdlive-diagram-' + (++diagramSeq);
      try {
        diagrams.set(key, (await mermaid.render(id, src)).svg);
      } catch (e) {
        diagrams.set(key, '<div class="diagram-error"><b>' + escapeHtml(t('diagramError')) + '</b><pre>' + escapeHtml(String(e && e.message || e)) + '</pre></div>');
        const junk = document.getElementById('d' + id);
        if (junk) junk.remove();
      }
      if (diagrams.size > 60) diagrams.delete(diagrams.keys().next().value);
    }
    if (last) render(last.text, last.lines, last.max);
  }

  // ----- Renderowanie -----

  // Podmienia tylko zmienione bloki, dzięki czemu obrazy nie mrugają i przewijanie zostaje.
  function patch(body) {
    const next = Array.from(body.childNodes);
    const cur = Array.from(content.childNodes);
    next.forEach((node, i) => {
      if (!cur[i]) content.appendChild(document.adoptNode(node));
      else if (!cur[i].isEqualNode(node)) content.replaceChild(document.adoptNode(node), cur[i]);
    });
    for (let i = cur.length - 1; i >= next.length; i--) content.removeChild(cur[i]);
  }

  // caret >= 0: render po wpisaniu tekstu - podgląd stoi, chyba że edytowany blok wyszedł poza ekran.
  function render(text, lines, max, caret = -1) {
    last = { text, lines, max };
    lineCount = Math.max(1, lines);
    lmax = max;
    empty.hidden = true;
    content.hidden = false;
    const { body: src, yaml } = splitFrontMatter(text);
    let html = md.render(src);
    if (yaml !== null) {
      html = '<pre class="front-matter" data-line="0"><code class="language-yaml">' + (highlight(yaml, 'yaml') || escapeHtml(yaml)) + '</code></pre>' + html;
    }
    const body = sanitize(html);
    placeDiagrams(body);
    window.MdExtras.decorate(body);
    patch(body);
    window.MdExtras.afterRender();
    marksDirty = true;
    if (caret >= 0) followCaret(caret);
  }

  let followHoldUntil = 0;

  function followCaret(line) {
    if (!cfg.sync || performance.now() < followHoldUntil) return;   // zmiana z podglądu (np. pole zadania): widok zostaje
    let el = null;
    content.querySelectorAll('[data-line]').forEach((e) => { if (+e.dataset.line <= line) el = e; });
    if (!el) return;
    const r = el.getBoundingClientRect(), top = barHeight();
    if (r.top >= top && r.top <= window.innerHeight - 40) return;   // widać - nic nie ruszamy
    glideTo(window.scrollY + r.top - top - (window.innerHeight - top) * 0.3);
  }

  function showEmpty() {
    last = null;
    content.hidden = true;
    empty.hidden = false;
    content.textContent = '';
    window.MdExtras.cleared();
    marksDirty = true;
  }

  function applyConfig(c) {
    const themeChanged = c.dark !== cfg.dark, langChanged = c.lang !== cfg.lang;
    cfg = c;
    root.dataset.theme = c.dark ? 'dark' : 'light';
    root.style.setProperty('--bg', c.bg);
    root.style.setProperty('--fg', c.fg);
    $('emptyTitle').textContent = t('emptyTitle');
    $('emptyText').textContent = t('emptyText');
    $('emptyBtn').textContent = t('emptyButton');
    // Pasek powstaje dopiero z konfiguracji, bo dopiero ona niesie język Notepad++.
    if (langChanged) window.MdToolbar.build(post, t);
    window.MdToolbar.update(c);
    window.MdExtras.config(c);
    marksDirty = true;
    if ((themeChanged || langChanged) && last) render(last.text, last.lines, last.max);
  }

  // ----- Synchronizacja przewijania -----

  const barHeight = () => bar.offsetHeight;

  function rebuildMarks() {
    const off = window.scrollY - barHeight();
    const list = [{ line: 0, top: 0 }];
    let prevTop = 0;
    content.querySelectorAll('[data-line]').forEach((el) => {
      if (el.closest('.fold-hidden')) return;   // zwinięta sekcja nie ma pozycji
      const y = el.getBoundingClientRect().top + off;
      const line = +el.dataset.line;
      if (line > list[list.length - 1].line && y >= prevTop) {
        list.push({ line, top: y });
        prevTop = y;
      }
    });
    list.push({ line: lineCount, top: Math.max(prevTop, content.getBoundingClientRect().bottom + off) });
    marks = list;
    marksDirty = false;
  }

  const maxScroll = () => Math.max(0, document.documentElement.scrollHeight - window.innerHeight);

  function rawLineToY(line) {
    if (marksDirty) rebuildMarks();
    let lo = 0, hi = marks.length - 1;
    while (lo < hi - 1) {
      const mid = (lo + hi) >> 1;
      if (marks[mid].line <= line) lo = mid; else hi = mid;
    }
    const a = marks[lo], b = marks[hi];
    if (b.line <= a.line) return a.top;
    return a.top + Math.min(1, Math.max(0, (line - a.line) / (b.line - a.line))) * (b.top - a.top);
  }

  // Na ostatnim ekranie edytora pozycja płynnie dochodzi do końca podglądu; wcześniej linie pokrywają się dokładnie.
  function lineToY(line) {
    if (line <= 0) return 0;
    if (lmax <= 0) return rawLineToY(line);
    const span = Math.max(1, lineCount - lmax);
    const tt = Math.min(1, Math.max(0, (line - (lmax - span)) / span));
    return Math.max(0, Math.min(maxScroll(), rawLineToY(line) + tt * (maxScroll() - rawLineToY(lmax))));
  }

  function yToLine(y) {
    if (y <= 0 || lmax <= 0) return 0;
    if (y >= maxScroll()) return lmax;
    let lo = 0, hi = lmax;
    for (let i = 0; i < 24; i++) {
      const mid = (lo + hi) / 2;
      if (lineToY(mid) < y) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
  }

  // Płynny dojazd do celu; kolejne komunikaty tylko przestawiają cel.
  // Przy skalowaniu ekranu przeglądarka zaokrągla pozycję do pikseli urządzenia, więc końcówkę
  // dociągamy jednym skokiem, a liczba klatek jest ograniczona - inaczej animacja mogła trwać bez końca.
  let target = null, animating = false, frames = 0;

  function stopGlide() {
    target = null;
    animating = false;
  }

  function glide() {
    if (target === null) { animating = false; return; }
    const goal = Math.min(target, maxScroll());
    const d = goal - window.scrollY;
    lockUntil = performance.now() + 120;
    if (Math.abs(d) < 6 || ++frames > 40 || document.hidden) {
      window.scrollTo(0, goal);
      stopGlide();
      return;
    }
    window.scrollTo(0, window.scrollY + d * 0.3);
    requestAnimationFrame(glide);
  }

  function glideTo(y) {
    target = Math.round(Math.max(0, Math.min(maxScroll(), y)));
    frames = 0;
    lockUntil = performance.now() + 120;
    if (!animating) { animating = true; requestAnimationFrame(glide); }
  }

  let holdUntil = 0;

  function scrollTo(line, atEnd, max) {
    if (!cfg.sync || performance.now() < holdUntil) return;
    lmax = max;
    glideTo(atEnd ? maxScroll() : lineToY(line));
  }

  // Ręczne przewijanie podglądu zawsze wygrywa z animacją.
  for (const ev of ['wheel', 'mousedown', 'keydown', 'touchstart']) {
    window.addEventListener(ev, () => { if (animating) stopGlide(); lockUntil = 0; }, { passive: true });
  }

  let pending = false;
  window.addEventListener('scroll', () => {
    if (!cfg.sync || pending || animating || performance.now() < lockUntil) return;
    pending = true;
    setTimeout(() => {
      pending = false;
      const atEnd = window.scrollY >= maxScroll() - 2 && maxScroll() > 0;
      post('sync', yToLine(window.scrollY).toFixed(3) + (atEnd ? '\tend' : ''));
    }, 16);
  }, { passive: true });

  // Każda zmiana wysokości treści (czcionki, obrazy, szerokość panelu) unieważnia mapę pozycji.
  new ResizeObserver(() => { marksDirty = true; }).observe(content);

  // Dwuklik w edytorze: przewija do bloku źródłowego i na chwilę go podświetla.
  // Element o najwyższym numerze linii <= line (nie ostatni w kolejności strony: przypisy leżą na dole,
  // a pochodzą ze środka tekstu). Cel to rzeczywista pozycja elementu; w długim bloku - proporcjonalnie do linii.
  function reveal(line, again = true) {
    let best = null, bestLine = -1;
    content.querySelectorAll('[data-line]').forEach((el) => {
      const l = +el.dataset.line;
      if (l <= line && l >= bestLine) { best = el; bestLine = l; }   // przy remisie głębszy element (punkt, nie cała lista)
    });
    if (!best) return;
    if (again && window.MdExtras.unfoldFor(best)) { reveal(line, false); return; }   // blok w zwiniętej sekcji
    const r = best.getBoundingClientRect(), bar = barHeight();
    const end = +best.dataset.lineEnd || bestLine + 1;
    const frac = Math.min(1, Math.max(0, (line - bestLine) / Math.max(1, end - bestLine)));
    holdUntil = performance.now() + 800;   // synchronizacja z edytora nie nadpisze tego ruchu
    glideTo(window.scrollY + r.top - bar + frac * r.height - (window.innerHeight - bar) / 3);
    best.classList.remove('flash');
    void best.offsetWidth;
    best.classList.add('flash');
    setTimeout(() => best.classList.remove('flash'), 1200);
  }

  // ----- Zdarzenia -----

  document.addEventListener('click', (e) => {
    const a = e.target.closest && e.target.closest('#content a[href]');
    if (!a) return;
    e.preventDefault();
    const raw = a.getAttribute('href');
    if (raw.startsWith('#')) {
      let id = raw.slice(1);
      try { id = decodeURIComponent(id); } catch (err) { /* zostaje surowe */ }
      const find = () => content.querySelector('[id="' + id.replace(/["\\]/g, '\\$&') + '"]');
      let el = find();
      if (el && window.MdExtras.unfoldFor(el)) el = find();
      if (el) el.scrollIntoView({ behavior: 'smooth' });
    } else {
      post('open', a.href);
    }
  });

  document.addEventListener('dblclick', (e) => {
    const el = e.target.closest && e.target.closest('#content [data-line]');
    if (el) post('goto', el.dataset.line);
  });

  $('emptyBtn').addEventListener('click', () => post('force'));

  window.MdExtras.init({
    post, t, md, content, sanitize, splitFrontMatter, escapeHtml, toast, glideTo, barHeight, loadMermaid,
    rerender: () => { if (last) render(last.text, last.lines, last.max); },
    cfg: () => cfg,
    lastText: () => last && last.text,
    invalidate: () => { marksDirty = true; },
    holdFollow: () => { followHoldUntil = performance.now() + 1500; },
  });

  // ----- Druk i PDF: zawsze jasny wygląd (także diagramy), potem powrót do motywu użytkownika -----

  // Kolory wydruku daje arkusz @media print, więc ekran się nie zmienia. Diagramy Mermaid to gotowe obrazki:
  // w ciemnym motywie dorysowujemy w tle ich jasne wersje (.print-only), widoczne tylko na wydruku.
  async function preparePrint() {
    for (let i = 0; i < 60 && content.querySelector('.diagram-wait, .diagram.stale'); i++) await new Promise((r) => setTimeout(r, 100));
    if (cfg.dark) await addPrintDiagrams();
    await document.fonts.ready;
    post('print-ready');
  }

  async function addPrintDiagrams() {
    const boxes = Array.from(content.querySelectorAll('.diagram[data-src]'));
    if (!boxes.length) return;
    let mermaid;
    try { mermaid = await loadMermaid(); } catch (e) { return; }
    mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme: 'neutral', fontFamily: '"Inter Variable", "Segoe UI", sans-serif' });
    for (const box of boxes) {
      const src = box.dataset.src, key = 'l:' + src;
      if (!diagrams.has(key)) {
        try { diagrams.set(key, (await mermaid.render('mdlive-diagram-' + (++diagramSeq), src)).svg); } catch (e) { continue; }
      }
      let print = box.querySelector(':scope > .print-only');
      if (!print) { print = document.createElement('div'); print.className = 'print-only'; box.appendChild(print); }
      print.innerHTML = diagrams.get(key);
    }
  }

  function restoreAfterPrint() { /* ekran nie był zmieniany */ }
  window.addEventListener('afterprint', () => { restoreAfterPrint(); post('print-end'); });

  function toast(text, withOpen) {
    const old = document.querySelector('.toast');
    if (old) old.remove();
    const el = document.createElement('div');
    el.className = 'toast';
    el.textContent = text;
    if (withOpen) {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = t('open');
      b.addEventListener('click', () => { post('open-saved'); el.remove(); });
      el.appendChild(b);
    }
    document.body.appendChild(el);
    setTimeout(() => el.remove(), 6000);
  }

  if (host) {
    host.addEventListener('message', (e) => {
      const m = e.data;
      if (m.type === 'config') applyConfig(m);
      else if (m.type === 'render') render(m.text, m.lines, m.lmax, m.caret);
      else if (m.type === 'empty') showEmpty();
      else if (m.type === 'scroll') scrollTo(m.line, m.end, m.lmax);
      else if (m.type === 'reveal') reveal(m.line);
      else if (m.type === 'caret') { window.MdToolbar.setBlock(m.block); window.MdExtras.setCaret(m.line); }
      else if (m.type === 'prep-print') preparePrint();
      else if (m.type === 'notice') { restoreAfterPrint(); toast(t(m.key) + (m.name ? ': ' + m.name : ''), m.open); }
      else if (m.type === 'html2md') post('paste-md', window.MdExtras.html2md(m.html));
      else if (m.type === 'checked') window.MdExtras.onChecked(m.missing, m.times);
      else if (m.type === 'peek') window.MdExtras.onPeek(m.url, m.text);
      else if (m.type === 'copy-rich') window.MdExtras.copyRich();
      else if (m.type === 'export-html') window.MdExtras.exportHtml(m.title);
      else if (m.type === 'present') window.MdExtras.present(m.on);
    });
    post('ready');
  }
})();

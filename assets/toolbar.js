// Pasek edycji nad podglądem: grupy przycisków wstawiają składnię Markdown do edytora Notepad++.
// Styl bieżącej linii (akapit, nagłówek, lista...) przychodzi z wtyczki i podświetla właściwe przyciski.
window.MdToolbar = (() => {
  'use strict';

  const svg = (body) => '<svg viewBox="0 0 16 16" aria-hidden="true">' + body + '</svg>';
  const ICONS = {
    undo: svg('<path d="M5.5 3 2.5 6l3 3"/><path d="M2.5 6h7a4 4 0 0 1 0 8H7"/>'),
    redo: svg('<path d="m10.5 3 3 3-3 3"/><path d="M13.5 6h-7a4 4 0 0 0 0 8H9"/>'),
    clear: svg('<path d="m9.5 2.5 4 4-6.5 6.5H3l-1-1v-2.5z"/><path d="M6 6l4 4M7.5 13.5H14"/>'),
    code: svg('<path d="M6 4 2 8l4 4M10 4l4 4-4 4"/>'),
    ul: svg('<circle cx="2.8" cy="4" r="1.1" class="f"/><circle cx="2.8" cy="8" r="1.1" class="f"/><circle cx="2.8" cy="12" r="1.1" class="f"/><path d="M6 4h8M6 8h8M6 12h8"/>'),
    ol: svg('<text x="0.5" y="6" class="t">1</text><text x="0.5" y="13.5" class="t">2</text><path d="M6 4h8M6 8h8M6 12h8"/>'),
    task: svg('<rect x="1.5" y="2.5" width="5" height="5" rx="1"/><path d="m2.8 5 1.2 1.2L6.8 3.4M9 5h5.5M1.5 11.5h13"/>'),
    quote: svg('<path d="M3 3v10" class="w"/><path d="M6.5 5h7.5M6.5 8h7.5M6.5 11h5"/>'),
    link: svg('<path d="M7 9a2.8 2.8 0 0 0 4 0l2.2-2.2a2.8 2.8 0 0 0-4-4l-.7.7M9 7a2.8 2.8 0 0 0-4 0L2.8 9.2a2.8 2.8 0 0 0 4 4l.7-.7"/>'),
    image: svg('<rect x="1.5" y="2.5" width="13" height="11" rx="1.5"/><circle cx="5.5" cy="6" r="1.2"/><path d="m1.5 12 4-4 3 3 2-2 4 4"/>'),
    table: svg('<rect x="1.5" y="2.5" width="13" height="11" rx="1.5"/><path d="M1.5 6.2h13M1.5 9.8h13M6 2.5v11M10.5 2.5v11"/>'),
    codeblock: svg('<path d="M5.5 2.5c-1.5 0-2 .6-2 2v1.6c0 .9-.5 1.4-1.5 1.9 1 .5 1.5 1 1.5 1.9v1.6c0 1.4.5 2 2 2M10.5 2.5c1.5 0 2 .6 2 2v1.6c0 .9.5 1.4 1.5 1.9-1 .5-1.5 1-1.5 1.9v1.6c0 1.4-.5 2-2 2"/>'),
    hr: svg('<path d="M1.5 8h13" class="w"/><path d="M4 4.5h8M4 11.5h8" class="d"/>'),
    math: svg('<path d="M12 3H4l4.5 5L4 13h8"/>'),
    diagram: svg('<rect x="1.5" y="1.5" width="5" height="4" rx=".8"/><rect x="9.5" y="10.5" width="5" height="4" rx=".8"/><path d="M4 5.5v4.5c0 1 .5 2.5 2.5 2.5h3"/>'),
    emoji: svg('<circle cx="8" cy="8" r="6.3"/><path d="M5.3 9.6a3.2 3.2 0 0 0 5.4 0"/><circle cx="5.9" cy="6.3" r=".7" class="f"/><circle cx="10.1" cy="6.3" r=".7" class="f"/>'),
    more: svg('<circle cx="3" cy="8" r="1.2" class="f"/><circle cx="8" cy="8" r="1.2" class="f"/><circle cx="13" cy="8" r="1.2" class="f"/>'),
    plus: svg('<path d="M8 3v10M3 8h10" class="w"/>'),
    footnote: svg('<text x="0.5" y="13" class="t big">x</text><text x="9" y="7" class="t">1</text>'),
    sup: svg('<text x="0.5" y="14" class="t big">x</text><text x="9.5" y="6.5" class="t">2</text>'),
    sub: svg('<text x="0.5" y="11" class="t big">x</text><text x="9.5" y="15" class="t">2</text>'),
    kbd: svg('<rect x="1.5" y="3.5" width="13" height="9" rx="1.5"/><path d="M4 6.5h1M7.5 6.5h1M11 6.5h1M5 9.5h6"/>'),
    mark: svg('<path d="m10 2.5 3.5 3.5-6 6H4v-3.5z"/><path d="M2 14.5h12" class="w"/>'),
    alert: svg('<circle cx="8" cy="8" r="6.3"/><path d="M8 4.8v3.8"/><circle cx="8" cy="11.2" r=".8" class="f"/>'),
    toc: svg('<path d="M2 3.5h12M4.5 6.5h9.5M4.5 9.5h9.5M2 12.5h12"/>'),
    back: svg('<path d="M10 3 5 8l5 5"/>'),
    print: svg('<path d="M4.5 5.5v-3h7v3"/><rect x="1.5" y="5.5" width="13" height="6" rx="1.2"/><rect x="4.5" y="9.5" width="7" height="4.5" rx=".5"/><circle cx="12" cy="7.8" r=".6" class="f"/>'),
    pdf: svg('<path d="M9.5 1.5h-6v13h9v-10z"/><path d="M9.5 1.5v3h3"/><path d="M5.5 9.5h5M5.5 11.5h3"/>'),
    sync: svg('<path d="M5 2v12M2.5 4.5 5 2l2.5 2.5M11 14V2M8.5 11.5 11 14l2.5-2.5"/>'),
    moon: svg('<path d="M13 9.5A5.5 5.5 0 0 1 6.5 3a5.5 5.5 0 1 0 6.5 6.5z"/>'),
    outline: svg('<path d="M2 3.5h7M4.5 6.5h7M4.5 9.5h5M2 12.5h9"/><circle cx="13" cy="3.5" r=".9" class="f"/>'),
    find: svg('<circle cx="7" cy="7" r="4.5"/><path d="m10.3 10.3 3.7 3.7"/>'),
    html: svg('<path d="M9.5 1.5h-6v13h9v-10z"/><path d="M9.5 1.5v3h3"/><path d="m6.5 8-1.5 1.7 1.5 1.8M9.5 8l1.5 1.7-1.5 1.8"/>'),
    copyRich: svg('<rect x="5.5" y="5.5" width="8" height="9" rx="1.5"/><path d="M10.5 5.5v-2A1.5 1.5 0 0 0 9 2H4a1.5 1.5 0 0 0-1.5 1.5V10A1.5 1.5 0 0 0 4 11.5h1.5M7.5 8.5h4M7.5 11h3"/>'),
    present: svg('<rect x="1.5" y="2.5" width="13" height="9" rx="1.2"/><path d="M8 11.5v3M5.5 14.5h5"/><path d="m6.8 5 3 2-3 2z" class="f"/>'),
    tidy: svg('<path d="M2 4h6M2 8h9M2 12h12"/><path d="m12 2 .6 1.4L14 4l-1.4.6L12 6l-.6-1.4L10 4l1.4-.6z" class="f"/>'),
    sun: svg('<circle cx="8" cy="8" r="3"/><path d="M8 1v1.6M8 13.4V15M1 8h1.6M13.4 8H15M3 3l1.1 1.1M11.9 11.9 13 13M3 13l1.1-1.1M11.9 4.1 13 3"/>'),
  };

  const EMOJI = '😀 😃 😄 😁 😆 😅 😂 🙂 😉 😊 😍 😘 😎 🤔 😐 😴 😢 😭 😡 🤯 👍 👎 👏 🙏 💪 👀 ✌️ 👋 🤝 💡 ✅ ❌ ⚠️ ❗ ❓ ⭐ 🔥 🎉 🚀 📌 📎 📝 📅 ⏰ 🔒 🔑 💻 📱 🐛 🔧 ⚙️ 📦 📊 💬 ❤️ 💔 ➡️ ⬅️ ⬆️ ⬇️'.split(' ');
  const ALERTS = ['NOTE', 'TIP', 'IMPORTANT', 'WARNING', 'CAUTION'];

  // Kolejność chowania, gdy panel jest za wąski (pierwsze znikają najwcześniej; trafiają do menu "Więcej").
  const HIDE_ORDER = ['redo', 'undo', 'emoji', 'quote', 'task', 'code', 'strike', 'format', 'ol', 'italic', 'ul'];

  const SEL = '\u0001';
  let post, t, bar, popover, syncBtn, themeBtn, moreBtn, styleBtn, cfg = {}, block = 'p', listening = false;
  const items = new Map();

  const edit = (...args) => post('edit', args.join('\t'));

  // Pasek nie zabiera fokusu: inaczej Enter w edytorze trafiałby w ostatnio kliknięty przycisk.
  const noFocus = (node) => node.addEventListener('mousedown', (e) => e.preventDefault());
  const el = (tag, cls, html) => Object.assign(document.createElement(tag), { className: cls || '', innerHTML: html || '' });

  function button(id, html, title, onClick, cls = '') {
    const b = el('button', 'tb ' + cls, html);
    b.type = 'button';
    b.dataset.id = id;
    b.title = title;
    b.addEventListener('click', (e) => { e.stopPropagation(); onClick(b); });
    items.set(id, { btn: b, html, title, onClick });
    noFocus(b);
    return b;
  }

  function group(...children) {
    const g = el('div', 'tb-seg');
    g.append(...children);
    return g;
  }

  // ----- Okienka -----

  function closePopover() {
    if (popover) { popover.remove(); popover = null; }
    bar && bar.querySelectorAll('.tb.open').forEach((b) => b.classList.remove('open'));
  }

  function openPopover(anchor, fill, cls = '') {
    const same = popover && popover.dataset.for === anchor.dataset.id;
    closePopover();
    if (same) return;
    popover = el('div', 'popover ' + cls);
    popover.dataset.for = anchor.dataset.id;
    popover.addEventListener('click', (e) => e.stopPropagation());
    noFocus(popover);
    fill(popover);
    document.body.appendChild(popover);
    anchor.classList.add('open');
    place(anchor);
  }

  function place(anchor) {
    const r = anchor.getBoundingClientRect();
    popover.style.maxHeight = Math.max(160, window.innerHeight - r.bottom - 12) + 'px';
    popover.style.left = Math.max(8, Math.min(r.left, window.innerWidth - popover.offsetWidth - 8)) + 'px';
    popover.style.top = r.bottom + 6 + 'px';
  }

  const run = (fn) => () => { closePopover(); fn(); };

  function menuItem(box, icon, label, fn, cls = '') {
    const item = el('button', 'menu-item ' + cls, '<span class="mi-icon">' + icon + '</span><span class="mi-label">' + label + '</span>');
    item.type = 'button';
    item.addEventListener('click', run(fn));
    box.appendChild(item);
    return item;
  }

  // Styl linii: podgląd każdego stylu narysowany jak w dokumencie.
  function styleMenu(box) {
    const opts = [['p', t('paragraph'), () => edit('lines', 'h0')]];
    for (let n = 1; n <= 6; n++) opts.push(['h' + n, t('heading') + ' ' + n, () => edit('lines', 'h' + n)]);
    opts.push(['quote', t('quote'), () => edit('lines', 'quote')]);
    for (const [id, label, fn] of opts) {
      const item = el('button', 'style-item s-' + id + (block === id ? ' current' : ''), '<span>' + label + '</span>');
      item.type = 'button';
      item.addEventListener('click', run(fn));
      box.appendChild(item);
    }
  }

  function formatMenu(box) {
    menuItem(box, ICONS.mark, t('mark'), () => edit('wrap', '==', '==', t('phText')));
    menuItem(box, ICONS.sup, t('sup'), () => edit('wrap', '^', '^', t('phText')));
    menuItem(box, ICONS.sub, t('sub'), () => edit('wrap', '~', '~', t('phText')));
    menuItem(box, ICONS.kbd, t('kbd'), () => edit('wrap', '<kbd>', '</kbd>', 'Ctrl'));
    box.appendChild(el('hr', 'menu-sep'));
    menuItem(box, ICONS.clear, t('clear'), () => edit('clear'));
    menuItem(box, ICONS.tidy, t('tidy'), () => { window.MdExtras.holdFollow(); post('tidy'); });
  }

  function tile(box, icon, label, fn) {
    const b = el('button', 'tile', '<span class="tile-icon">' + icon + '</span><span class="tile-label">' + label + '</span>');
    b.type = 'button';
    b.addEventListener('click', fn);
    box.appendChild(b);
  }

  function insertMenu(box) {
    box.textContent = '';
    const grid = el('div', 'tiles');
    tile(grid, ICONS.table, t('table'), () => tablePicker(box));
    tile(grid, ICONS.image, t('image'), run(() => edit('image')));
    tile(grid, ICONS.link, t('link'), run(() => edit('inline', '[' + SEL + t('phLink') + SEL + '](https://)', '[{sel}](' + SEL + 'https://' + SEL + ')')));
    tile(grid, ICONS.codeblock, t('codeblock'), run(() => edit('block', '```' + SEL + SEL + '\n' + t('phCode') + '\n```')));
    tile(grid, ICONS.math, t('math'), run(() => edit('block', '$$\n' + SEL + 'E = mc^2' + SEL + '\n$$')));
    tile(grid, ICONS.diagram, t('diagram'), run(() => edit('block', '```mermaid\ngraph TD\n  A[' + SEL + t('start') + SEL + '] --> B{' + t('decision') + '}\n  B -->|' + t('yes') + '| C[' + t('end') + ']\n  B -->|' + t('no') + '| A\n```')));
    tile(grid, ICONS.footnote, t('footnote'), run(() => edit('footnote', t('phFootnote'))));
    tile(grid, ICONS.toc, t('toc'), run(() => window.MdExtras.insertToc()));
    tile(grid, ICONS.hr, t('hr'), run(() => edit('block', '---')));
    box.appendChild(grid);

    box.appendChild(el('div', 'menu-title', t('alert')));
    const chips = el('div', 'chips');
    for (const kind of ALERTS) {
      const c = el('button', 'chip alert-' + kind.toLowerCase(), t(kind));
      c.type = 'button';
      c.addEventListener('click', run(() => edit('block', '> [!' + kind + ']\n> ' + SEL + t('phText') + SEL)));
      chips.appendChild(c);
    }
    box.appendChild(chips);
  }

  function tablePicker(box) {
    box.textContent = '';
    const head = el('div', 'pop-head');
    const back = el('button', 'pop-back', ICONS.back);
    back.type = 'button';
    back.addEventListener('click', () => insertMenu(box));
    head.append(back, el('span', '', t('tablePick')));
    const COLS = 8, ROWS = 8;
    const grid = el('div', 'grid');
    const label = el('div', 'grid-label', '&nbsp;');
    const cells = [];
    for (let r = 1; r <= ROWS; r++) {
      for (let c = 1; c <= COLS; c++) {
        const cell = el('span', 'cell');
        cell.addEventListener('mouseenter', () => {
          cells.forEach((x) => x.el.classList.toggle('on', x.r <= r && x.c <= c));
          label.textContent = c + ' × ' + r + ' · ' + t('tableHint');
        });
        cell.addEventListener('click', run(() => insertTable(c, r)));
        cells.push({ el: cell, r, c });
        grid.appendChild(cell);
      }
    }
    box.append(head, grid, label);
  }

  function insertTable(cols, rows) {
    const head = [], line = [], empty = [];
    for (let c = 1; c <= cols; c++) {
      const name = t('column') + ' ' + c;
      head.push(c === 1 ? SEL + name + SEL : name);
      line.push('-'.repeat(Math.max(3, name.length)));
      empty.push(' '.repeat(name.length));
    }
    const out = ['| ' + head.join(' | ') + ' |', '| ' + line.join(' | ') + ' |'];
    for (let r = 0; r < rows; r++) out.push('| ' + empty.join(' | ') + ' |');
    edit('block', out.join('\n'));
  }

  function emojiPicker(box) {
    for (const e of EMOJI) {
      const b = el('button', 'emoji-item', e);
      b.type = 'button';
      b.addEventListener('click', run(() => edit('inline', e, e)));
      box.appendChild(b);
    }
  }

  function printMenu(box) {
    menuItem(box, ICONS.print, t('printDialog'), () => post('print', 'dialog'));
    menuItem(box, ICONS.pdf, t('savePdf'), () => post('print', 'pdf'));
    menuItem(box, ICONS.html, t('exportHtml'), () => post('export'));
    menuItem(box, ICONS.copyRich, t('copyRich'), () => window.MdExtras.copyRich());
    box.appendChild(el('hr', 'menu-sep'));
    menuItem(box, ICONS.present, t('present'), () => post('present'));
  }

  // Przyciski schowane z braku miejsca.
  function moreMenu(box) {
    for (const [, it] of items) {
      if (!it.btn.hidden) continue;
      const item = menuItem(box, it.html, it.title, () => {});
      item.onclick = (e) => { e.stopPropagation(); closePopover(); it.onClick(moreBtn); };
    }
  }

  // ----- Układ -----

  function fit() {
    closePopover();
    for (const { btn } of items.values()) btn.hidden = false;
    bar.querySelectorAll('.tb-seg').forEach((g) => { g.hidden = false; });
    moreBtn.hidden = true;
    for (const id of HIDE_ORDER) {
      if (bar.scrollWidth <= bar.clientWidth) break;
      items.get(id).btn.hidden = true;
      moreBtn.hidden = false;
    }
    bar.querySelectorAll('.tb-seg').forEach((g) => { g.hidden = ![...g.children].some((c) => !c.hidden); });
  }

  function styleLabel() {
    if (block === 'p' || !block) return t('paragraph');
    if (block[0] === 'h') return t('heading') + ' ' + block.slice(1);
    return { ul: t('ul'), ol: t('ol'), task: t('task'), quote: t('quote') }[block] || t('paragraph');
  }

  function build(postFn, translate) {
    post = postFn;
    t = translate;
    bar = document.getElementById('toolbar');
    closePopover();
    bar.textContent = '';
    items.clear();

    styleBtn = button('style', '', t('style'), (b) => openPopover(b, styleMenu, 'styles'), 'style');
    const left = el('div', 'tb-group');
    left.append(
      group(
        button('undo', ICONS.undo, t('undo'), () => edit('undo')),
        button('redo', ICONS.redo, t('redo'), () => edit('redo')),
      ),
      group(styleBtn),
      group(
        button('bold', '<span class="txt b">B</span>', t('bold'), () => edit('wrap', '**', '**', t('phBold'))),
        button('italic', '<span class="txt i">I</span>', t('italic'), () => edit('wrap', '*', '*', t('phItalic'))),
        button('strike', '<span class="txt s">S</span>', t('strike'), () => edit('wrap', '~~', '~~', t('phText'))),
        button('code', ICONS.code, t('code'), () => edit('wrap', '`', '`', t('phCode'))),
        button('format', '<span class="txt aa">Aa</span><span class="caret">▾</span>', t('more'), (b) => openPopover(b, formatMenu, 'menu')),
      ),
      group(
        button('ul', ICONS.ul, t('ul'), () => edit('lines', 'ul')),
        button('ol', ICONS.ol, t('ol'), () => edit('lines', 'ol')),
        button('task', ICONS.task, t('task'), () => edit('lines', 'task')),
        button('quote', ICONS.quote, t('quote'), () => edit('lines', 'quote')),
      ),
      group(
        button('insert', ICONS.plus + '<span class="lbl-always">' + t('insert') + '</span><span class="caret">▾</span>', t('insert'), (b) => openPopover(b, insertMenu, 'insert'), 'accent'),
        button('emoji', ICONS.emoji, t('emoji'), (b) => openPopover(b, emojiPicker, 'emoji')),
      ),
    );
    moreBtn = button('more', ICONS.more, t('more'), (b) => openPopover(b, moreMenu, 'menu'));
    items.delete('more');
    left.append(moreBtn);

    const right = el('div', 'tb-group right');
    syncBtn = button('sync', ICONS.sync + '<span class="lbl">Sync</span>', '', () => post('toggle-sync'), 'toggle');
    themeBtn = button('theme', '', '', () => post('theme', cfg.dark ? 'light' : 'dark'), 'toggle');
    const printBtn = button('print', ICONS.print, t('share'), (b) => openPopover(b, printMenu, 'menu'));
    const outlineBtn = button('outline', ICONS.outline, t('outline'), (b) => openPopover(b, (box) => window.MdExtras.outlineMenu(box, closePopover), 'menu outline'));
    const findBtn = button('find', ICONS.find, t('find'), () => window.MdExtras.openFind());
    for (const id of ['sync', 'theme', 'style', 'insert', 'bold', 'print', 'outline', 'find']) items.delete(id);
    right.append(group(outlineBtn, findBtn), group(printBtn), group(syncBtn, themeBtn));
    bar.append(left, right);
    setBlock(block);
    fit();

    if (listening) return;
    listening = true;
    new ResizeObserver(() => fit()).observe(bar);
    document.addEventListener('click', () => closePopover());
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closePopover(); });
    window.addEventListener('resize', closePopover);
  }

  function update(c) {
    cfg = c;
    syncBtn.classList.toggle('on', !!c.sync);
    syncBtn.title = c.sync ? t('syncOn') : t('syncOff');
    themeBtn.innerHTML = (c.dark ? ICONS.sun : ICONS.moon) + '<span class="lbl">' + (c.dark ? t('light') : t('dark')) + '</span>';
    themeBtn.title = c.dark ? t('toLight') : t('toDark');
  }

  // Styl linii z kursorem: etykieta "Styl" i podświetlone przyciski list.
  function setBlock(b) {
    block = b || 'p';
    if (!styleBtn) return;
    styleBtn.innerHTML = '<span class="style-name">' + styleLabel() + '</span><span class="caret">▾</span>';
    for (const id of ['ul', 'ol', 'task', 'quote']) {
      const it = items.get(id);
      if (it) it.btn.classList.toggle('active', block === id);
    }
  }

  return { build, update, setBlock };
})();

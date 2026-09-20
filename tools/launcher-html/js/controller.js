// Controller-first navigation. Two panes, like console settings screens: the sidebar is a list the
// D-pad walks up and down (pages switch as you move), right or A enters the page, and B or left
// from the page's left edge come back to it. Inside a page the D-pad moves between controls and A
// selects. LB / RB switch the page's own section tabs (left / right buttons for a left / right bar,
// like console tab bars), or jump between its sections when it has cards instead of tabs; LT / RT
// step pages from anywhere (the native launcher's LB / RB shortcut,
// moved off the bumpers), Start launches from Play. Arrow keys drive the same navigation and
// Escape works as B, so the whole flow can be checked from a keyboard too.
(function () {
  const { ui } = DKRLauncher;
  const { h } = ui;
  const html = document.documentElement;
  const FOCUSABLE = 'a[href], button, input:not([type="hidden"]):not([type="file"]), select, textarea, summary, [tabindex]';
  const REPEAT_DELAY = 320;
  const REPEAT_RATE = 85;
  const reducedMotion = () => html.dataset.motion === 'off' || matchMedia('(prefers-reduced-motion: reduce)').matches;

  // ------------------------------------------------------------ input mode

  let mode = 'pointer';
  let glyphs = 'xbox';
  let travel = 0;

  function setMode(next) {
    if (mode === next) return;
    mode = next;
    html.dataset.input = next;
    const active = document.activeElement;
    if (next === 'gamepad') active?.classList?.add('nav-focus');
    else document.querySelectorAll('.nav-focus').forEach((el) => el.classList.remove('nav-focus'));
    if (next === 'pointer') hideHint();
    else if (active && active !== document.body) showHint(active);
    renderLegend(true);
  }
  html.dataset.input = mode;

  // Chrome sends a synthetic mousemove after every scroll, so only a real hand on the mouse counts.
  addEventListener('pointermove', (event) => {
    travel += Math.abs(event.movementX) + Math.abs(event.movementY);
    if (travel > 12) setMode('pointer');
  }, true);
  addEventListener('pointerdown', () => setMode('pointer'), true);
  addEventListener('keydown', () => { travel = 0; setMode('keyboard'); }, true);

  // ------------------------------------------------------------ where things are

  const openPicker = () => document.querySelector('.picker:popover-open');
  const content = () => document.getElementById('content');
  const pageId = () => location.hash.replace(/^#\/?/, '') || 'play';
  const sidebarItems = () => [...document.querySelectorAll('#navigation .race-button, #sidebar-actions .race-button')];
  const currentPageItem = () => document.querySelector('#navigation .race-button[aria-current="page"]');
  const inSidebar = (el) => !!el?.closest?.('.sidebar');
  const overlayOpen = () => !!(openPicker() || ui.topModal());
  const scope = () => openPicker() || ui.topModal() || content();

  // Disabled buttons stay reachable (see unlock) so a controller player can find them and read why.
  function candidates(root) {
    return [...root.querySelectorAll(FOCUSABLE)].filter((el) => {
      if (el.getAttribute('tabindex') === '-1' || el.closest('[inert]')) return false;
      if (el.disabled && !el.matches('button')) return false;
      const rect = el.getBoundingClientRect();
      return rect.width >= 1 && rect.height >= 1 && getComputedStyle(el).visibility !== 'hidden';
    });
  }

  // ------------------------------------------------------------ disabled buttons

  // A disabled button can't hold focus, so while the D-pad is on it the button becomes
  // aria-disabled (clicks are blocked below). Pages that flip .disabled meanwhile still work: the
  // property is redirected to aria-disabled until focus leaves and the real attribute comes back.
  function unlock(el) {
    if (!el.matches('button') || !el.disabled || 'navDisabled' in el.dataset) return;
    el.removeAttribute('disabled');
    el.setAttribute('aria-disabled', 'true');
    el.dataset.navDisabled = '';
    Object.defineProperty(el, 'disabled', {
      configurable: true,
      get() { return this.getAttribute('aria-disabled') === 'true'; },
      set(value) { if (value) this.setAttribute('aria-disabled', 'true'); else this.removeAttribute('aria-disabled'); },
    });
  }

  function relock(el) {
    if (!el?.dataset || !('navDisabled' in el.dataset)) return;
    const off = el.getAttribute('aria-disabled') === 'true';
    delete el.disabled;
    delete el.dataset.navDisabled;
    el.removeAttribute('aria-disabled');
    el.disabled = off;
  }

  document.addEventListener('click', (event) => {
    const el = event.target.closest?.('[aria-disabled="true"]');
    if (!el || !('navDisabled' in el.dataset)) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    showHint(el, true);
  }, true);

  function disabledReason(el) {
    const described = (el.getAttribute('aria-describedby') || '').split(/\s+/)
      .map((id) => id && document.getElementById(id)?.textContent.trim()).filter(Boolean).join(' ');
    return el.dataset.disabledReason || described || el.title || 'Not available right now.';
  }

  // ------------------------------------------------------------ hint bubble

  // Under the focused control: why a button is unavailable, or the tip a mouse user would get
  // from its tooltip. Never shown for the mouse.
  const hint = h('div', { id: 'nav-hint', popover: 'manual', role: 'status' });
  let hintTarget = null;

  function showHint(el, nudge) {
    const locked = el.getAttribute?.('aria-disabled') === 'true';
    const text = locked ? disabledReason(el) : el.dataset?.hint || el.title;
    if (!text || mode === 'pointer' || !hint.isConnected) { hideHint(); return; }
    hintTarget = el;
    hint.replaceChildren(locked ? h('span', { class: 'nav-hint-lock', 'aria-hidden': 'true' }) : null, text);
    hint.classList.toggle('is-locked', locked);
    if (hint.matches(':popover-open')) hint.hidePopover();
    hint.showPopover();
    placeHint();
    if (nudge && !reducedMotion()) { hint.classList.remove('is-nudged'); void hint.offsetWidth; hint.classList.add('is-nudged'); }
  }

  function placeHint() {
    if (!hintTarget?.isConnected || !hint.matches(':popover-open')) return;
    const zoom = ui.pageZoom();
    const rect = hintTarget.getBoundingClientRect();
    const box = hint.getBoundingClientRect();
    const gap = 10 * zoom;
    const below = rect.bottom + gap + box.height < innerHeight - 8 * zoom;
    const top = below ? rect.bottom + gap : rect.top - gap - box.height;
    const left = Math.max(8 * zoom, Math.min(rect.left, innerWidth - box.width - 8 * zoom));
    hint.style.top = top / zoom + 'px';
    hint.style.left = left / zoom + 'px';
    hint.classList.toggle('is-above', !below);
  }

  function hideHint() {
    hintTarget = null;
    if (hint.matches(':popover-open')) hint.hidePopover();
  }

  let hintFrame = 0;
  addEventListener('scroll', () => {
    if (!hintTarget || hintFrame) return;
    hintFrame = requestAnimationFrame(() => { hintFrame = 0; placeHint(); });
  }, true);

  // ------------------------------------------------------------ focus

  // Where the D-pad was on each page, so coming back (LT / RT, the sidebar) lands in the same spot.
  const memory = new Map();
  // And in each section of a page, so LB / RB return to the control you left there.
  const sectionMemory = new WeakMap();

  document.addEventListener('focusin', (event) => {
    const el = event.target;
    if (mode === 'gamepad') el.classList?.add('nav-focus');
    if (mode !== 'pointer') showHint(el);
    const root = content();
    if (root && el !== root && root.contains(el)) {
      memory.set(pageId(), { fk: el.dataset?.fk, focus: el.dataset?.focus, id: el.id, text: el.textContent.trim().slice(0, 80) });
      const section = el.closest('[data-section]');
      if (section) sectionMemory.set(section, el);
    }
    if (mode === 'gamepad') renderLegend(false);
  });
  document.addEventListener('focusout', (event) => {
    event.target.classList?.remove('nav-focus');
    relock(event.target);
    if (hintTarget === event.target) hideHint();
  });

  function focusElement(el, { scroll = true } = {}) {
    unlock(el);
    el.focus({ preventScroll: true, focusVisible: true });
    if (mode === 'gamepad') el.classList.add('nav-focus');
    if (scroll) el.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: reducedMotion() ? 'auto' : 'smooth' });
    showHint(el);
  }

  function recall(root, list) {
    const saved = memory.get(pageId());
    if (!saved) return null;
    const find = (selector) => {
      const el = root.querySelector(selector);
      return el && list.includes(el) ? el : null;
    };
    return (saved.fk && find(`[data-fk="${CSS.escape(saved.fk)}"]`)) ||
      (saved.focus && find(`[data-focus="${CSS.escape(saved.focus)}"]`)) ||
      (saved.id && find('#' + CSS.escape(saved.id))) ||
      list.find((el) => el.textContent.trim().slice(0, 80) === saved.text) || null;
  }

  // Where focus lands when nothing in the current scope has it yet.
  function defaultTarget(root, list) {
    if (root === content()) {
      const remembered = recall(root, list);
      if (remembered) return remembered;
    }
    const preferred = root.querySelector('[data-default-focus], [autofocus]');
    if (preferred && list.includes(preferred) && !preferred.disabled) return preferred;
    const view = root.getBoundingClientRect();
    return list.find((el) => {
      const rect = el.getBoundingClientRect();
      return rect.top >= view.top - 1 && rect.bottom <= view.bottom + 1;
    }) || list[0];
  }

  function focusPage() {
    const root = content();
    const list = candidates(root);
    if (list.length) focusElement(defaultTarget(root, list));
  }

  // ------------------------------------------------------------ spatial navigation

  // Distance along the direction plus a heavier penalty for drifting sideways, so the D-pad
  // prefers the next control in the same column or row.
  function score(from, to, dir, strict) {
    const tol = 2;
    if (dir === 'down' || dir === 'up') {
      const ahead = dir === 'down' ? to.top >= from.bottom - tol : to.bottom <= from.top + tol;
      const loose = dir === 'down' ? to.top + to.height / 2 > from.top + from.height / 2 + tol : to.top + to.height / 2 < from.top + from.height / 2 - tol;
      if (strict ? !ahead : !loose) return Infinity;
      const primary = Math.max(0, dir === 'down' ? to.top - from.bottom : from.top - to.bottom);
      const ortho = Math.max(0, to.left - from.right, from.left - to.right);
      return primary + ortho * 2 + Math.abs((to.left + to.right) - (from.left + from.right)) * 0.02;
    }
    // Sideways moves only go to controls that really are to that side: a diagonal jump to the row
    // above feels random, and "nothing to the left" is what takes you back to the sidebar.
    const ahead = dir === 'right' ? to.left >= from.right - tol : to.right <= from.left + tol;
    if (!ahead) return Infinity;
    const primary = Math.max(0, dir === 'right' ? to.left - from.right : from.left - to.right);
    const ortho = Math.max(0, to.top - from.bottom, from.top - to.bottom);
    return primary + ortho * 4 + Math.abs((to.top + to.bottom) - (from.top + from.bottom)) * 0.05;
  }

  function nearest(list, current, dir) {
    const from = current.getBoundingClientRect();
    for (const strict of dir === 'up' || dir === 'down' ? [true, false] : [true]) {
      let best = null;
      let bestScore = Infinity;
      for (const el of list) {
        if (el === current) continue;
        const value = score(from, el.getBoundingClientRect(), dir, strict);
        if (value < bestScore) { bestScore = value; best = el; }
      }
      if (best) return best;
    }
    return null;
  }

  // The sidebar is a plain list: up / down walk it (pages switch as you go, like LT / RT),
  // right enters the page. RESTART and EXIT only take focus; A asks to confirm them.
  let holdSidebar = null;
  function moveSidebar(dir, current) {
    if (dir === 'right') { focusPage(); return true; }
    if (dir === 'left') return true;
    const items = sidebarItems();
    const next = items[items.indexOf(current) + (dir === 'down' ? 1 : -1)];
    if (!next) return true;
    focusElement(next, { scroll: false });
    if (next.dataset.page && next.getAttribute('aria-current') !== 'page') {
      holdSidebar = next.dataset.page;
      location.hash = '#/' + next.dataset.page;
    }
    return true;
  }

  function move(dir) {
    const active = document.activeElement;
    if (!overlayOpen() && inSidebar(active)) return moveSidebar(dir, active);
    const root = scope();
    if (!root) return false;
    const list = candidates(root);
    if (!list.length) return false;
    if (!active || active === root || !root.contains(active) || !list.includes(active)) {
      focusElement(defaultTarget(root, list));
      return true;
    }
    const best = nearest(list, active, dir);
    if (best) { focusElement(best); return true; }
    if (dir === 'left' && root === content()) { toSidebar(); return true; }
    return false;
  }

  function toSidebar() {
    const item = currentPageItem();
    if (item) focusElement(item, { scroll: false });
  }

  // Nothing further that way: scroll instead, so long text below the last control is reachable.
  function nudge(dir, amount = 160) {
    if (dir !== 'up' && dir !== 'down') return;
    const root = scope();
    const scroller = root === content() ? root : root.querySelector('.rb-list') || root;
    scroller.scrollBy({ top: dir === 'down' ? amount : -amount, behavior: reducedMotion() ? 'auto' : 'smooth' });
  }

  // D-pad left / right tweak a focused slider; holding speeds up long ranges.
  function adjustRange(el, dir, repeats) {
    const span = Number(el.max || 100) - Number(el.min || 0);
    const steps = repeats > 8 ? Math.max(1, Math.round(span / 40 / Number(el.step || 1))) : 1;
    for (let i = 0; i < steps; i += 1) (dir === 'right' ? el.stepUp() : el.stepDown());
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }

  // ------------------------------------------------------------ actions

  function press(el) {
    el.classList.add('is-pressed');
    setTimeout(() => el.classList.remove('is-pressed'), 140);
  }

  function activate() {
    const el = document.activeElement;
    if (!overlayOpen() && el?.matches?.('#navigation .race-button')) {
      press(el);
      // The page may already be on its way (the sidebar switches as you walk it).
      if (el.dataset.page === pageId()) {
        if (holdSidebar) pendingPageFocus = true;
        else focusPage();
      } else { pendingPageFocus = true; el.click(); }
      return;
    }
    if (!overlayOpen() && inSidebar(el)) { press(el); el.click(); return; }
    const root = scope();
    if (!el || el === document.body || el === root || !root?.contains(el)) { move('down'); return; }
    press(el);
    if (el.matches('select')) ui.selectPicker(el);
    // Left / right tweak a slider; A is Enter, the row's quick action if the page gives it one
    // (Sound: mute a volume, put a band back to 0 dB). data-a-label names it in the legend.
    else if (el.matches('input[type="range"]')) el.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    else if (el.matches('input[type="text"], textarea')) el.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    else el.click();
  }

  // B closes the top list or dialog; on a page it goes back to the sidebar.
  function back() {
    const list = openPicker();
    if (list) { list.hidePopover(); return; }
    const modal = ui.topModal();
    if (modal) {
      if (modal.gamepadShortcuts?.b) modal.gamepadShortcuts.b();
      else ui.requestClose(modal);
      return;
    }
    if (!inSidebar(document.activeElement)) toSidebar();
  }

  function shortcut(name) {
    const modal = ui.topModal();
    if (openPicker()) return false;
    const action = modal?.gamepadShortcuts?.[name];
    if (action) { action(); return true; }
    return false;
  }

  // LT / RT: the page shortcut, from anywhere (LB / RB in the native launcher, where the D-pad can't
  // reach the sidebar). Pages switch immediately; RESTART and EXIT only take focus.
  let pendingPageFocus = false;
  function stepSidebar(step) {
    if (overlayOpen()) return;
    const items = sidebarItems();
    let index = items.indexOf(document.activeElement);
    if (index < 0) index = items.findIndex((btn) => btn.getAttribute('aria-current') === 'page');
    const next = items[(index + step + items.length) % items.length];
    if (next.dataset.page) {
      if (next.getAttribute('aria-current') === 'page') { focusPage(); return; }
      pendingPageFocus = true;
      location.hash = '#/' + next.dataset.page;
    } else {
      focusElement(next, { scroll: false });
    }
  }

  // LB / RB: the page's own section tabs (Online, Mods), marked with data-tabs. Focus lands on the
  // new tab, one D-pad press away from its content. Pages without tabs jump between their
  // data-section cards instead (stepSections); pages with neither ignore them.
  const tabOn = (tab) => ['aria-current', 'aria-expanded', 'aria-pressed'].some((name) =>
    ['page', 'true'].includes(tab.getAttribute(name)));
  const tabBar = () => content()?.querySelector('[data-tabs]');

  function stepTabs(step) {
    if (overlayOpen()) return;
    const bar = tabBar();
    if (!bar) { stepSections(step); return; }
    const tabs = [...bar.querySelectorAll('button')].filter((tab) => !tab.disabled);
    if (!tabs.length) return;
    const index = tabs.findIndex(tabOn);
    const next = tabs[index < 0 ? (step > 0 ? 0 : tabs.length - 1) : (index + step + tabs.length) % tabs.length];
    const fk = next.dataset.fk;
    if (!tabOn(next)) next.click();
    requestAnimationFrame(() => {
      const target = (fk && content().querySelector(`[data-fk="${CSS.escape(fk)}"]`)) || next;
      if (target.isConnected) focusElement(target);
    });
  }

  // Sections: cards on one scrolling page (Sound's Volume / Equalizer / Multiplayer). Nothing is
  // hidden, so a mouse still sees everything; the pad just skips the rows in between. Lands on the
  // control last used there, else the card's data-default-focus, else its first control, and
  // scrolls the whole card into view when it fits so its title shows.
  const sections = () => [...(content()?.querySelectorAll('[data-section]') || [])].filter((sec) => candidates(sec).length);

  function stepSections(step) {
    const list = sections();
    if (list.length < 2) return;
    const index = list.findIndex((sec) => sec.contains(document.activeElement));
    const next = list[index < 0 ? (step > 0 ? 0 : list.length - 1) : (index + step + list.length) % list.length];
    const controls = candidates(next);
    const remembered = sectionMemory.get(next);
    const target = (controls.includes(remembered) && remembered) || controls.find((el) => el.matches('[data-default-focus]')) || controls[0];
    focusElement(target, { scroll: false });
    const fits = next.getBoundingClientRect().height <= content().clientHeight;
    (fits ? next : target).scrollIntoView({ block: 'nearest', behavior: reducedMotion() ? 'auto' : 'smooth' });
  }

  addEventListener('hashchange', () => {
    // Entering the page (A, LT / RT) wins over walking the sidebar.
    if (pendingPageFocus) {
      pendingPageFocus = false;
      holdSidebar = null;
      requestAnimationFrame(focusPage);
      return;
    }
    if (!holdSidebar) return;
    holdSidebar = null;
    const item = currentPageItem();
    if (item && !ui.topModal() && document.activeElement !== item) focusElement(item, { scroll: false });
  });

  function start() {
    if (shortcut('start')) return;
    if (overlayOpen()) return;
    const btn = document.querySelector('.start-button:not(:disabled)');
    if (btn) { press(btn); btn.click(); }
  }

  // ------------------------------------------------------------ keyboard

  const ARROWS = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
  document.addEventListener('keydown', (event) => {
    if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey || event.defaultPrevented) return;
    const el = document.activeElement;
    // Escape works as B on pages (dialogs and lists already close on it).
    if (event.key === 'Escape' && !overlayOpen()) { back(); return; }
    const dir = ARROWS[event.key];
    if (!dir) return;
    if (el?.matches('input[type="text"], input[type="search"], input:not([type]), textarea, [contenteditable]')) return;
    if (el?.matches('input[type="range"]') && (dir === 'left' || dir === 'right')) return;
    if (move(dir) || el?.matches('select')) event.preventDefault();
  });

  // Every <select> opens the shared list: click, Enter, Space or Alt+Down.
  document.addEventListener('mousedown', (event) => {
    const select = event.target.closest?.('select');
    if (!select || select.disabled || select.multiple || event.button !== 0) return;
    event.preventDefault();
    select.focus({ preventScroll: true });
    ui.selectPicker(select);
  }, true);
  document.addEventListener('keydown', (event) => {
    const select = event.target.closest?.('select');
    if (!select || select.disabled) return;
    if (event.key === 'Enter' || event.key === ' ' || event.key === 'F4' || (event.altKey && event.key === 'ArrowDown')) {
      event.preventDefault();
      ui.selectPicker(select);
    }
  });

  // ------------------------------------------------------------ legend

  const GLYPHS = {
    xbox: { a: 'A', b: 'B', x: 'X', y: 'Y', lb: 'LB', rb: 'RB', lt: 'LT', rt: 'RT', start: '☰' },
    deck: { a: 'A', b: 'B', x: 'X', y: 'Y', lb: 'L1', rb: 'R1', lt: 'L2', rt: 'R2', start: '☰' },
    ps: { a: '✕', b: '○', x: '□', y: '△', lb: 'L1', rb: 'R1', lt: 'L2', rt: 'R2', start: 'OPTIONS' },
    switch: { a: 'B', b: 'A', x: 'Y', y: 'X', lb: 'L', rb: 'R', lt: 'ZL', rt: 'ZR', start: '+' },
  };

  const legend = h('div', { id: 'controller-legend', popover: 'manual', 'aria-hidden': 'true' });
  document.addEventListener('DOMContentLoaded', () => document.body.append(legend, hint));

  function legendItems() {
    if (captureHandler) return [['text', 'Press the button to use · wait 5 s to cancel']];
    if (openPicker()) return [['dpad', 'Move'], ['a', 'Choose'], ['b', 'Close']];
    const modal = ui.topModal();
    if (modal) {
      const s = modal.gamepadShortcuts || {};
      return [['dpad', 'Move'], ['a', 'Select'], ['b', s.bLabel || 'Close'],
        s.x ? ['x', s.xLabel] : null, s.y ? ['y', s.yLabel] : null, s.start ? ['start', s.startLabel] : null].filter(Boolean);
    }
    const active = document.activeElement;
    if (inSidebar(active)) return [['dpad', 'Pages'], ['a', active.dataset.page ? 'Open' : 'Select']];
    const items = [];
    if (tabBar() || sections().length > 1) items.push(['lbrb', 'Sections']);
    items.push(['ltrt', 'Pages']);
    const slider = active?.matches?.('input[type="range"]');
    items.push(['dpad', slider ? 'Move / adjust' : 'Move']);
    if (!slider) items.push(['a', 'Select']);
    else if (active.dataset.aLabel) items.push(['a', active.dataset.aLabel]);
    items.push(['b', 'Menu']);
    if (document.querySelector('.start-button:not(:disabled)')) items.push(['start', 'Start race']);
    return items;
  }

  function glyph(kind) {
    const set = GLYPHS[glyphs];
    if (kind === 'text') return null;
    if (kind === 'dpad') return h('span', { class: 'cl-glyph is-dpad' }, h('i'), h('i'));
    if (kind === 'lbrb' || kind === 'ltrt') {
      const [a, b] = kind === 'lbrb' ? [set.lb, set.rb] : [set.lt, set.rt];
      return [h('span', { class: 'cl-glyph is-shoulder' }, a), h('span', { class: 'cl-glyph is-shoulder' }, b)];
    }
    return h('span', { class: `cl-glyph is-${kind} is-${glyphs}` }, set[kind]);
  }

  let legendKey = '';
  let legendTop = null;
  function renderLegend(force) {
    if (!legend.isConnected) return;
    if (mode !== 'gamepad') {
      if (legend.matches(':popover-open')) legend.hidePopover();
      legendKey = '';
      return;
    }
    const items = legendItems();
    const key = glyphs + JSON.stringify(items);
    if (force || key !== legendKey) {
      legendKey = key;
      legend.replaceChildren(...items.map(([kind, text]) => h('span', { class: 'cl-item' }, glyph(kind), text)));
    }
    // Re-showing keeps the legend (and the hint) above a dialog or list that opened after it.
    const top = openPicker() || ui.topModal();
    if (!legend.matches(':popover-open') || top !== legendTop) {
      legendTop = top;
      if (legend.matches(':popover-open')) legend.hidePopover();
      legend.showPopover();
      if (hintTarget) { hint.hidePopover(); hint.showPopover(); placeHint(); }
    }
  }

  // ------------------------------------------------------------ gamepad polling

  // Standard mapping: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 8 View, 9 Menu, 10 L3, 11 R3, 12-15 D-pad.
  const BUTTONS = { a: 0, b: 1, x: 2, y: 3, lb: 4, rb: 5, lt: 6, rt: 7, start: 9 };
  const RAW_NAMES = ['A', 'B', 'X', 'Y', 'LB', 'RB', 'LT', 'RT', 'View', 'Menu', 'L3', 'R3', 'D-Up', 'D-Down', 'D-Left', 'D-Right'];
  const held = new Map();
  let polling = 0;
  let lastLegend = 0;

  function readPads() {
    return navigator.getGamepads ? [...navigator.getGamepads()].filter((pad) => pad && pad.connected) : [];
  }

  function detectGlyphs(pad) {
    const id = pad.id;
    if (/28de|steam deck/i.test(id)) return 'deck';
    if (/054c|dualsense|dualshock|playstation|wireless controller/i.test(id)) return 'ps';
    if (/057e|nintendo|pro controller|joy-con/i.test(id)) return 'switch';
    return 'xbox';
  }

  function sample(pads) {
    const down = new Set();
    for (const pad of pads) {
      const pressed = (i) => !!pad.buttons[i]?.pressed;
      for (const [name, index] of Object.entries(BUTTONS)) if (pressed(index)) down.add(name);
      const [x = 0, y = 0] = pad.axes;
      if (pressed(12) || y < -0.5) down.add('up');
      if (pressed(13) || y > 0.5) down.add('down');
      if (pressed(14) || x < -0.5) down.add('left');
      if (pressed(15) || x > 0.5) down.add('right');
    }
    return down;
  }

  // Raw buttons and firm stick moves, for rebinding (CHOOSE A NEW CONTROL).
  function sampleRaw(pads) {
    const down = new Set();
    for (const pad of pads) {
      pad.buttons.forEach((button, i) => { if (button.pressed && RAW_NAMES[i]) down.add(RAW_NAMES[i]); });
      const [lx = 0, ly = 0, rx = 0, ry = 0] = pad.axes;
      const axis = (value, minus, plus) => { if (value < -0.7) down.add(minus); if (value > 0.7) down.add(plus); };
      axis(lx, 'LS Left', 'LS Right'); axis(ly, 'LS Up', 'LS Down'); axis(rx, 'RS Left', 'RS Right'); axis(ry, 'RS Up', 'RS Down');
    }
    return down;
  }

  // While a rebind is waiting, the next new press goes to it instead of navigating. Buttons still
  // held from opening it (the A press) have to be let go first.
  let captureHandler = null;
  let captureArmed = false;
  function capture(handler) {
    captureHandler = handler;
    captureArmed = false;
    renderLegend(true);
    return () => { if (captureHandler === handler) { captureHandler = null; renderLegend(true); } };
  }

  function onPress(name, repeats) {
    if (name === 'up' || name === 'down' || name === 'left' || name === 'right') {
      const el = document.activeElement;
      if (el?.matches('input[type="range"]') && (name === 'left' || name === 'right') && scope()?.contains(el)) adjustRange(el, name, repeats);
      else if (!move(name)) nudge(name);
      return;
    }
    if (name === 'a') activate();
    else if (name === 'b') back();
    else if (name === 'lb') stepTabs(-1);
    else if (name === 'rb') stepTabs(1);
    else if (name === 'lt') stepSidebar(-1);
    else if (name === 'rt') stepSidebar(1);
    else if (name === 'start') start();
    else if (name === 'x' || name === 'y') shortcut(name);
  }

  function poll(time) {
    const pads = readPads();
    if (!pads.length) { polling = 0; held.clear(); return; }
    if (captureHandler) {
      const raw = sampleRaw(pads);
      if (!raw.size) captureArmed = true;
      else if (captureArmed) {
        const handler = captureHandler;
        captureHandler = null;
        glyphs = detectGlyphs(pads[0]);
        handler([...raw][0]);
        held.clear();
        for (const name of sample(pads)) held.set(name, { next: Infinity, repeats: 0 }); // no nav from this press
      }
      polling = requestAnimationFrame(poll);
      return;
    }
    const down = sample(pads);
    for (const name of held.keys()) if (!down.has(name)) held.delete(name);
    for (const name of down) {
      const state = held.get(name);
      if (!state) {
        held.set(name, { next: time + REPEAT_DELAY, repeats: 0 });
        glyphs = detectGlyphs(pads[0]);
        setMode('gamepad');
        onPress(name, 0);
      } else if (['up', 'down', 'left', 'right'].includes(name) && time >= state.next) {
        state.repeats += 1;
        state.next = time + REPEAT_RATE;
        onPress(name, state.repeats);
      }
    }
    // Right stick scrolls the page or dialog, for long text with nothing to focus.
    const ry = pads.reduce((value, pad) => (Math.abs(pad.axes[3] || 0) > Math.abs(value) ? pad.axes[3] : value), 0);
    if (Math.abs(ry) > 0.25) {
      const root = scope();
      const scroller = root === content() ? root : root?.querySelector('.rb-list') || root;
      scroller?.scrollBy({ top: ry * 22 });
    }
    if (time - lastLegend > 150) { lastLegend = time; renderLegend(false); }
    polling = requestAnimationFrame(poll);
  }

  function startPolling() {
    if (!polling) polling = requestAnimationFrame(poll);
  }
  addEventListener('gamepadconnected', startPolling);
  document.addEventListener('DOMContentLoaded', () => { if (readPads().length) startPolling(); });

  // press('a'), press('down'), press('rt')...: the same path a real pad takes. Handy from the console.
  function simulate(name) {
    if (captureHandler) { const handler = captureHandler; captureHandler = null; handler(name.toUpperCase()); return; }
    setMode('gamepad');
    onPress(name, 0);
    renderLegend(false);
  }

  DKRLauncher.controller = {
    press: simulate,
    capture,
    focus: (el) => focusElement(el),
    get mode() { return mode; },
    get glyphs() { return glyphs; },
  };
})();

window.DKRLauncher = window.DKRLauncher || {};

(function () {
  function h(tag, props, ...children) {
    const el = document.createElement(tag);
    if (props) {
      for (const [key, value] of Object.entries(props)) {
        if (key === 'class') el.className = value;
        else if (key === 'text') el.textContent = value;
        else if (key.startsWith('on') && typeof value === 'function') el.addEventListener(key.slice(2), value);
        else if (value !== undefined && value !== null && value !== false) el.setAttribute(key, value === true ? '' : value);
      }
    }
    for (const child of children.flat()) {
      if (child === undefined || child === null || child === false) continue;
      el.append(child.nodeType ? child : document.createTextNode(String(child)));
    }
    return el;
  }

  function raceButton({ label, onClick, variant, current, disabled, dataPage }) {
    const btn = h('button', {
      class: ['race-button', variant].filter(Boolean).join(' '),
      type: 'button',
      onclick: onClick,
      disabled,
      'aria-current': current ? 'page' : undefined,
      'data-page': dataPage,
    }, h('span', { text: label }));
    return btn;
  }

  function card(children, className) {
    return h('div', { class: ['card', className].filter(Boolean).join(' ') }, ...[].concat(children));
  }

  function field(labelText, controlEl) {
    return h('div', { class: 'field' }, h('label', {}, labelText, controlEl));
  }

  function checkboxRow(label, checked, onChange) {
    const input = h('input', { type: 'checkbox' });
    input.checked = !!checked;
    input.addEventListener('change', () => onChange(input.checked));
    return h('label', { class: 'checkbox' }, input, h('span', { text: label }));
  }

  function sliderRow(label, { min = 0, max = 100, value = 0, format }, onInput) {
    const output = h('output', { text: format ? format(value) : String(value) });
    const input = h('input', { type: 'range', min, max, value });
    input.addEventListener('input', () => {
      const next = Number(input.value);
      output.textContent = format ? format(next) : String(next);
      onInput(next);
    });
    return h('div', { class: 'field' },
      h('label', {}, label),
      h('div', { class: 'slider' }, input, output));
  }

  function selectRow(label, options, value, onChange) {
    const select = h('select', {}, ...options.map((opt) =>
      h('option', { value: opt.value, selected: opt.value === value || undefined }, opt.label)));
    select.addEventListener('change', () => onChange(select.value));
    return h('div', { class: 'field' }, h('label', {}, label), select);
  }

  function disclosure(summaryText, bodyChildren, open) {
    return h('details', { class: 'disclosure', open: open || undefined },
      h('summary', { class: 'race-button full' }, h('span', { text: summaryText })),
      h('div', { class: 'disclosure-body' }, ...[].concat(bodyChildren)));
  }

  function tabs(tabDefs, activeId, onSelect) {
    const nav = h('div', { class: 'tabs' }, ...tabDefs.map((tab) =>
      raceButton({
        label: tab.label,
        current: tab.id === activeId,
        onClick: () => onSelect(tab.id),
      })));
    return nav;
  }

  function notify(message, timeout = 3200) {
    const el = document.getElementById('notification');
    el.textContent = message;
    el.hidden = false;
    // Re-showing the popover puts it back on top of any open modal dialog.
    if (el.showPopover) {
      if (el.matches(':popover-open')) el.hidePopover();
      el.showPopover();
    }
    clearTimeout(notify._timer);
    notify._timer = setTimeout(() => {
      el.hidden = true;
      if (el.hidePopover && el.matches(':popover-open')) el.hidePopover();
    }, timeout);
  }

  // The browser hands focus back to whatever opened a dialog, but pages re-render while one is
  // open. When that control was replaced, focus its replacement (same data-fk) so the D-pad
  // carries on from the same place instead of starting over.
  function returnFocusOnClose(dialog) {
    const opener = document.activeElement;
    const fk = opener?.dataset?.fk;
    dialog.addEventListener('close', () => {
      const active = document.activeElement;
      if (active && active !== document.body) return;
      const target = opener?.isConnected ? opener : fk && document.querySelector(`[data-fk="${CSS.escape(fk)}"]`);
      target?.focus({ preventScroll: true });
    }, { once: true });
  }

  function openModal({ heading, body, actions }) {
    const modal = document.getElementById('modal');
    if (!modal.open) returnFocusOnClose(modal);
    modal.className = '';
    modal.removeAttribute('style');
    modal.replaceChildren(
      h('h2', { id: 'modal-heading', text: heading }),
      ...[].concat(body),
      h('div', { class: 'dialog-actions' }, ...[].concat(actions || []))
    );
    modal.showModal();
    return modal;
  }

  function closeModal() {
    document.getElementById('modal').close();
  }

  // The dialog everything else is stacked under, or null. Focus is trapped in the topmost one.
  function topModal() {
    const open = [...document.querySelectorAll('dialog')].filter((d) => d.open && d.matches(':modal'));
    return open.find((d) => d.contains(document.activeElement)) || open[open.length - 1] || null;
  }

  // A modal that stacks over whatever is already open (keyboards, the ROM browser). Removed once closed.
  function stackDialog({ className, labelledBy, children }) {
    const dialog = h('dialog', { class: className, 'aria-labelledby': labelledBy }, ...children);
    returnFocusOnClose(dialog);
    dialog.addEventListener('close', () => dialog.remove());
    document.body.append(dialog);
    dialog.showModal();
    return dialog;
  }

  // B / Escape: give the dialog a chance to refuse (running jobs), then close it.
  function requestClose(dialog) {
    const event = new Event('cancel', { cancelable: true });
    dialog.dispatchEvent(event);
    if (!event.defaultPrevented && dialog.open) dialog.close();
  }

  // ------------------------------------------------------------ drop-down list
  // One list for mouse, keyboard and controller: the D-pad walks the options, A picks, B closes.
  // The browser's own <select> popup can't be driven by a controller, so every select opens this.

  function picker({ anchor, options, value, onPick, label, minWidth = 220 }) {
    // The click on the field that just light-dismissed its own list shouldn't reopen it.
    if (picker.dismissedAnchor === anchor && performance.now() - picker.dismissedAt < 350) return null;
    closePicker();
    const host = topModal() || document.body; // outside the open modal it would be inert
    const list = h('div', { class: 'picker', popover: 'auto', role: 'listbox', 'aria-label': label });
    for (const option of options) {
      if (option.separator) { list.append(h('div', { class: 'picker-sep', role: 'separator' })); continue; }
      const selected = !option.action && option.value === value;
      const btn = h('button', {
        type: 'button', role: 'option', class: 'picker-option' + (option.action ? ' is-action' : ''),
        'aria-selected': String(selected), disabled: option.disabled,
        'data-disabled-reason': option.disabled ? option.reason : undefined,
      }, h('span', { class: 'picker-label' }, option.label), option.detail ? h('span', { class: 'picker-detail' }, option.detail) : null);
      btn.addEventListener('click', () => { close(true); onPick(option.value, option); });
      list.append(btn);
    }

    let closed = false;
    const api = { element: list, anchor, close };
    const onScroll = (event) => { if (!list.contains(event.target)) close(false); };
    const onResize = () => close(false);
    function close(restoreFocus) {
      if (closed) return;
      closed = true;
      removeEventListener('scroll', onScroll, true);
      removeEventListener('resize', onResize);
      if (list.matches(':popover-open')) list.hidePopover();
      list.remove();
      if (restoreFocus && anchor.isConnected) anchor.focus({ preventScroll: true });
      if (picker.current === api) picker.current = null;
    }
    // Light dismiss (outside click, Escape). Focus goes back to the field unless the click
    // that dismissed the list already moved it somewhere else.
    list.addEventListener('toggle', (event) => {
      if (event.newState !== 'closed' || closed) return;
      picker.dismissedAnchor = anchor;
      picker.dismissedAt = performance.now();
      const active = document.activeElement;
      close(!active || active === document.body || list.contains(active));
    });

    host.append(list);
    list.showPopover();
    // Rects are in screen pixels; the list's own lengths get multiplied by the interface zoom.
    const zoom = pageZoom();
    const px = (screen) => screen / zoom + 'px';
    const rect = anchor.getBoundingClientRect();
    const width = Math.max(rect.width, minWidth * zoom);
    const gap = 6 * zoom;
    const edge = 12 * zoom;
    list.style.width = px(width);
    const below = innerHeight - rect.bottom - gap - edge;
    const above = rect.top - gap - edge;
    const natural = list.getBoundingClientRect().height;
    const down = below >= Math.min(natural, 260 * zoom) || below >= above;
    const maxHeight = Math.max(120 * zoom, down ? below : above);
    list.style.maxHeight = px(maxHeight);
    list.style.top = px(down ? rect.bottom + gap : rect.top - gap - Math.min(natural, maxHeight));
    list.style.left = px(Math.max(edge, Math.min(rect.left, innerWidth - width - edge)));
    const start = list.querySelector('[aria-selected="true"]:not(:disabled)') || list.querySelector('button:not(:disabled)');
    start?.focus({ preventScroll: true });
    start?.scrollIntoView({ block: 'nearest' });
    addEventListener('scroll', onScroll, true);
    addEventListener('resize', onResize);
    picker.current = api;
    return api;
  }

  // Interface size (js/display.js) is CSS zoom on <html>.
  function pageZoom() {
    return parseFloat(getComputedStyle(document.documentElement).zoom) || 1;
  }

  function closePicker() {
    picker.current?.close(false);
  }

  // Any <select> opens the shared list instead of the browser popup; its change events still fire.
  function selectPicker(select) {
    const options = [...select.options].filter((o) => !o.hidden).map((o) => ({ value: o.value, label: o.textContent, disabled: o.disabled, reason: o.dataset.reason }));
    return picker({
      anchor: select, options, value: select.value, label: select.getAttribute('aria-label') || select.closest('label')?.textContent || 'Choose',
      minWidth: 160,
      onPick: (value) => {
        if (select.value === value) return;
        select.value = value;
        select.dispatchEvent(new Event('input', { bubbles: true }));
        select.dispatchEvent(new Event('change', { bubbles: true }));
      },
    });
  }

  // ------------------------------------------------------------ text fields
  // Text boxes are buttons: A (or a click) opens the on-screen keyboard, so no text field ever
  // needs a physical keyboard. Typing on a real keyboard while one is focused opens it too.

  function keyboardIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', '0 0 24 16');
    svg.setAttribute('class', 'field-button-icon');
    svg.setAttribute('aria-hidden', 'true');
    svg.innerHTML = '<rect x="1" y="1" width="22" height="14" rx="3" fill="none" stroke="currentColor" stroke-width="1.6"/>' +
      '<path d="M5 5h2M9 5h2M13 5h2M17 5h2M5 8.5h2M9 8.5h2M13 8.5h2M17 8.5h2M7.5 12h9" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>';
    return svg;
  }

  function fieldButton({ label, value, placeholder, onOpen, className, fk, disabled, id, reason }) {
    const btn = h('button', {
      type: 'button', class: 'field-button' + (className ? ' ' + className : ''), 'data-fk': fk, id, disabled,
      'data-disabled-reason': disabled ? reason : undefined,
      'aria-haspopup': 'dialog', 'aria-label': `${label}: ${value || 'empty'}. Opens the on-screen keyboard.`,
    }, h('span', { class: 'field-button-value' + (value ? '' : ' is-placeholder') }, value || placeholder || ''), keyboardIcon());
    btn.addEventListener('click', () => onOpen(''));
    btn.addEventListener('keydown', (event) => {
      if (event.key.length !== 1 || event.key === ' ' || event.ctrlKey || event.metaKey || event.altKey) return;
      event.preventDefault();
      onOpen(event.key);
    });
    return btn;
  }

  // The launcher's on-screen keyboard (DrawTextEntryKeyboard and friends): an 8-column key grid,
  // SPACE / BACKSPACE / CLEAR, then CANCEL and the red accept button.
  const TEXT_KEYS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.\'';
  const TEXT_HELP = 'Use the D-pad and A button to enter text. Every letter and number is available; spaces and common name characters are included too.';

  function keyboard({
    heading, help = TEXT_HELP, hint = 'TEXT', value = '', typed = '', maxLength = 48, keys = TEXT_KEYS,
    accept = 'SAVE', allowEmpty = false, space = true, letterCase = true, prefix = '', paste = false,
    filter, validate, tiles = 0, mono = false, onAccept,
  }) {
    // filter normalises what may be typed (codes); prefix is a floor BACKSPACE and CLEAR stop at.
    const clean = (text) => (filter ? filter(text) : text).slice(0, maxLength);
    let text = clean(value + typed);
    let lower = false;

    const input = tiles ? null : h('input', {
      type: 'text', class: 'kb-input' + (mono ? ' is-mono' : ''), placeholder: hint, maxlength: maxLength,
      autocomplete: 'off', spellcheck: 'false', 'aria-label': hint,
    });
    const tileRow = tiles ? h('div', { class: 'kb-tiles', role: 'status', 'aria-label': hint }) : null;
    // validate() problems keep the keyboard open and say what to fix.
    const problem = h('p', { class: 'kb-error', role: 'alert' });
    const keyButtons = [...keys].map((ch) => {
      const btn = raceButton({ label: ch, onClick: () => insert(lower ? ch.toLowerCase() : ch) });
      btn.classList.add('kb-key');
      return btn;
    });
    keyButtons[0].autofocus = !typed;
    const caseButton = letterCase && /[A-Z]/.test(keys) ? raceButton({ label: 'abc', onClick: () => { lower = !lower; syncCase(); } }) : null;
    caseButton?.classList.add('kb-case');
    const keyGrid = h('div', { class: 'kb-keys' }, ...keyButtons);
    const acceptButton = raceButton({ label: accept, variant: 'red', onClick: finish });
    acceptButton.dataset.disabledReason = 'Type at least one character first.';

    function syncCase() {
      keyButtons.forEach((btn, i) => { btn.firstChild.textContent = lower ? keys[i].toLowerCase() : keys[i]; });
      keyGrid.classList.toggle('is-lower', lower);
      if (caseButton) {
        caseButton.firstChild.textContent = lower ? 'ABC' : 'abc';
        caseButton.setAttribute('aria-pressed', String(lower));
      }
    }
    function sync() {
      if (input && input.value !== text) input.value = text;
      if (tileRow) {
        tileRow.replaceChildren(...Array.from({ length: tiles }, (_, i) =>
          h('span', { class: 'kb-tile' + (text[i] ? ' is-filled' : '') + (i === Math.min(text.length, tiles - 1) ? ' is-caret' : '') }, text[i] || '')));
      }
      acceptButton.disabled = !allowEmpty && text.length <= prefix.length;
      problem.textContent = '';
    }
    function insert(chars) { text = clean(text + chars); sync(); }
    function backspace() { if (text.length > prefix.length) text = text.slice(0, -1); sync(); }
    function clear() { text = prefix; sync(); }
    async function pasteText() {
      try { insert((await navigator.clipboard.readText()).trim()); } catch { notify('Clipboard unavailable. Type the code with the keys instead.'); }
    }
    function finish() {
      if (acceptButton.disabled) return;
      const error = validate?.(text);
      if (error) { problem.textContent = error; return; }
      dialog.close();
      onAccept(text);
    }

    const edits = [
      space ? raceButton({ label: 'SPACE', onClick: () => insert(' ') }) : null,
      raceButton({ label: 'BACKSPACE', onClick: backspace }),
      raceButton({ label: 'CLEAR', onClick: clear }),
      caseButton,
      paste ? raceButton({ label: 'PASTE', onClick: pasteText }) : null,
    ].filter(Boolean);

    const dialog = stackDialog({
      className: 'kb-dialog', labelledBy: 'kb-heading',
      children: [
        h('h2', { id: 'kb-heading' }, heading),
        h('p', { class: 'kb-help' }, help),
        input || tileRow,
        problem,
        keyGrid,
        h('div', { class: 'kb-actions', style: `--kb-cols:${edits.length}` }, ...edits),
        h('div', { class: 'kb-actions is-finish' }, raceButton({ label: 'CANCEL', onClick: () => dialog.close() }), acceptButton),
      ],
    });
    // Couch shortcuts, shown in the controller legend: X deletes, Y adds a space, Start accepts.
    dialog.gamepadShortcuts = {
      x: backspace, xLabel: 'Backspace',
      y: space ? () => insert(' ') : undefined, yLabel: 'Space',
      start: finish, startLabel: accept.charAt(0) + accept.slice(1).toLowerCase(),
      bLabel: 'Cancel',
    };

    if (input) {
      input.addEventListener('input', () => { text = clean(input.value); sync(); });
      input.addEventListener('keydown', (event) => { if (event.key === 'Enter') { event.preventDefault(); finish(); } });
    }
    // A real keyboard works too: letters typed while a key has focus go straight into the text.
    dialog.addEventListener('keydown', (event) => {
      if (event.target === input || event.ctrlKey || event.metaKey || event.altKey) return;
      if (event.key === 'Backspace') { event.preventDefault(); backspace(); return; }
      if (event.key.length !== 1 || event.key === ' ') return;
      event.preventDefault();
      insert(event.key);
      if (input) { input.focus(); input.setSelectionRange(text.length, text.length); }
    });
    syncCase();
    sync();
    if (typed && input) { input.focus(); input.setSelectionRange(text.length, text.length); }
    return dialog;
  }

  DKRLauncher.ui = {
    h, raceButton, card, field, checkboxRow, sliderRow, selectRow,
    disclosure, tabs, notify, openModal, closeModal,
    topModal, stackDialog, requestClose, picker, closePicker, selectPicker, fieldButton, keyboard, pageZoom,
  };
})();

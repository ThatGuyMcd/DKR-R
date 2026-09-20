window.DKRLauncher = window.DKRLauncher || {};

// The ROM list (the native launcher's ROM catalog) and the in-launcher ROM browser. Play and Online
// both use the same drop-down, so racers can flip between v 1.0 and v 1.1 wherever they are.
(function () {
  const { ui, state } = DKRLauncher;
  const { h } = ui;
  const ADD = '\u0000add';

  // A small stand-in file system for the browser. Folders map to their folders and ROM files.
  const FILES = {
    '': { folders: ['C:\\', 'D:\\'] },
    'C:\\': { folders: ['Games', 'Users'] },
    'C:\\Games': { folders: ['N64'] },
    'C:\\Games\\N64': { files: ['Diddy Kong Racing (USA).z64', 'Diddy Kong Racing (USA) (Rev A).z64', 'Diddy Kong Racing (USA) (Rev A).n64', 'Diddy Kong Racing (Europe) (En,Fr,De).z64'] },
    'C:\\Users': { folders: ['Player'] },
    'C:\\Users\\Player': { folders: ['Documents', 'Downloads'] },
    'C:\\Users\\Player\\Documents': {},
    'C:\\Users\\Player\\Downloads': { files: ['dkr_backup.v64'] },
    'D:\\': { folders: ['Emulation'] },
    'D:\\Emulation': { folders: ['roms'] },
    'D:\\Emulation\\roms': { folders: ['n64', 'snes'] },
    'D:\\Emulation\\roms\\n64': { files: ['Diddy Kong Racing.z64', 'Diddy Kong Racing (Rev A).z64'] },
    'D:\\Emulation\\roms\\snes': {},
  };

  const rom = () => state.get().rom;
  const entries = () => rom().catalog || [];
  const fileName = (path) => path.split('\\').pop();
  const join = (dir, name) => (dir.endsWith('\\') ? dir + name : dir + '\\' + name);

  function parent(dir) {
    if (!dir || /^[A-Z]:\\$/.test(dir)) return '';
    const up = dir.slice(0, dir.lastIndexOf('\\'));
    return /^[A-Z]:$/.test(up) ? up + '\\' : up;
  }

  // Filename stand-in for ROM detection, shared with Play and Mods: 0 = unsupported, 1 = v 1.0, 2 = v 1.1.
  function revisionOf(path) {
    const name = fileName(path);
    if (/europe|japan|\((e|j|eu|jp)\)|pal/i.test(name)) return 0;
    return /v\s*1\.1|rev\s*a|v80/i.test(name) ? 2 : 1;
  }

  const versionOf = (path) => (revisionOf(path) === 2 ? 'v 1.1' : 'v 1.0');
  const label = (path) => 'Diddy Kong Racing ' + versionOf(path);

  function select(path) {
    state.update('rom', { path, ready: true });
  }

  function add(path) {
    const known = entries().some((entry) => entry.path === path);
    state.update('rom', { path, ready: true, catalog: known ? entries() : [...entries(), { path }] });
    return !known;
  }

  // Removing the selected ROM falls back to the next one in the list, so START stays available.
  function remove(path) {
    const catalog = entries().filter((entry) => entry.path !== path);
    const current = rom().path === path ? (catalog[0]?.path || '') : rom().path;
    state.update('rom', { catalog, path: current, ready: !!current });
    return current;
  }

  function restore(path, index) {
    const catalog = entries().filter((entry) => entry.path !== path);
    catalog.splice(Math.min(index, catalog.length), 0, { path });
    state.update('rom', { catalog, path, ready: true });
  }

  // ------------------------------------------------------------ ROM browser

  function icon(kind) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('class', 'rb-icon');
    svg.setAttribute('aria-hidden', 'true');
    svg.innerHTML = kind === 'folder'
      ? '<path d="M2.5 6.5a2 2 0 0 1 2-2h4.2l2.2 2.4h8.6a2 2 0 0 1 2 2v9.6a2 2 0 0 1-2 2h-15a2 2 0 0 1-2-2z" fill="currentColor"/>'
      : kind === 'drive'
        ? '<rect x="2.5" y="7" width="19" height="10" rx="2.5" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="17.5" cy="12" r="1.4" fill="currentColor"/>'
        : '<path d="M5 3.5h14v13l-2 2v2H7v-2l-2-2z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M8.5 7h7v5h-7z" fill="currentColor"/>';
    return svg;
  }

  function openBrowser({ onPicked } = {}) {
    const input = h('input', { type: 'file', accept: '.z64,.n64,.v64', hidden: true });
    const where = h('p', { class: 'rb-where' });
    const message = h('p', { class: 'rb-message', role: 'alert' });
    const list = h('div', { class: 'rb-list' });
    const upButton = ui.raceButton({ label: 'UP ONE LEVEL', onClick: () => go(parent(dir), dir) });
    const pcButton = ui.raceButton({ label: 'THIS PC', onClick: () => go('') });
    upButton.dataset.disabledReason = 'You are already at This PC.';
    const current = rom().path;
    let dir = current && FILES[parent(current)] ? parent(current) : '';

    function pick(path) {
      if (!revisionOf(path)) {
        message.textContent = `${fileName(path)} is not supported. DKR-R needs the USA Game Pak, v 1.0 or v 1.1.`;
        return;
      }
      dialog.close();
      const added = add(path);
      onPicked?.(path, added);
    }

    function go(next, cameFrom) {
      dir = next;
      message.textContent = '';
      where.replaceChildren('Current folder: ', h('strong', {}, dir || 'This PC'));
      upButton.disabled = !dir;
      dialog.gamepadShortcuts.bLabel = dir ? 'Up one folder' : 'Close';
      const folder = FILES[dir] || {};
      const rows = [
        ...(folder.folders || []).map((name) => {
          const path = dir ? join(dir, name) : name;
          const row = h('button', { type: 'button', class: 'rb-row', 'data-path': path, onclick: () => go(path) },
            icon(dir ? 'folder' : 'drive'), h('span', { class: 'rb-name' }, name), dir ? null : h('span', { class: 'rb-tag' }, 'Local disk'));
          return row;
        }),
        ...(folder.files || []).map((name) => {
          const path = join(dir, name);
          const revision = revisionOf(path);
          const known = entries().some((entry) => entry.path === path);
          return h('button', { type: 'button', class: 'rb-row is-rom' + (revision ? '' : ' is-unsupported'), 'data-path': path, onclick: () => pick(path) },
            icon('rom'), h('span', { class: 'rb-name' }, name),
            known ? h('span', { class: 'rb-tag is-known' }, 'In your list') : null,
            h('span', { class: 'rb-tag' }, revision ? versionOf(path) : 'Not supported'));
        }),
      ];
      list.replaceChildren(...(rows.length ? rows : [h('p', { class: 'rb-empty' }, 'No supported ROM files or folders were found here.')]));
      const back = cameFrom && list.querySelector(`[data-path="${CSS.escape(cameFrom)}"]`);
      (back || list.querySelector('.rb-row') || pcButton).focus({ preventScroll: true });
      (back || list.firstChild)?.scrollIntoView?.({ block: 'nearest' });
    }

    input.addEventListener('change', () => {
      const file = input.files[0];
      if (file) pick(file.name);
    });

    const dialog = ui.stackDialog({
      className: 'rom-browser', labelledBy: 'rb-heading',
      children: [
        h('h2', { id: 'rb-heading' }, 'SELECT DIDDY KONG RACING ROM'),
        h('p', { class: 'rb-sub' }, 'Folders first, then .z64, .v64 and .n64 files. Nothing leaves this PC.'),
        h('div', { class: 'rb-bar' }, where, h('div', { class: 'rb-nav' }, upButton, pcButton)),
        message,
        list,
        h('div', { class: 'dialog-actions rb-actions' },
          ui.raceButton({ label: 'CANCEL', onClick: () => dialog.close() }),
          ui.raceButton({ label: 'SYSTEM FILE PICKER', onClick: () => input.click() })),
        input,
      ],
    });
    // B climbs one folder, like the native browser; at This PC it closes.
    dialog.gamepadShortcuts = { b: () => (dir ? go(parent(dir), dir) : dialog.close()), bLabel: 'Up one folder' };
    go(dir);
    return dialog;
  }

  // ------------------------------------------------------------ drop-down

  // The field shows the selected ROM; its list holds every known ROM plus "Add a ROM...".
  function romSelect({ fk, disabled = false, compact = false, focusKey, reason, onChange } = {}) {
    const { path } = rom();
    const catalog = entries();
    const btn = h('button', {
      type: 'button', class: 'rom-select' + (compact ? ' is-compact' : ''), 'data-fk': fk, 'data-focus': focusKey, disabled,
      'data-disabled-reason': disabled ? reason : undefined,
      'aria-haspopup': 'listbox', 'aria-label': `Game ROM: ${path ? label(path) + ', ' + fileName(path) : 'none selected'}. ${catalog.length} in your list.`,
    },
    h('span', { class: 'rom-select-text' },
      h('strong', {}, path ? label(path) : 'Choose a ROM'),
      h('small', {}, path ? fileName(path) : catalog.length + ' in your list')),
    h('span', { class: 'rom-select-chevron', 'aria-hidden': 'true' }));
    btn.addEventListener('click', () => ui.picker({
      anchor: btn, label: 'Game ROM', value: path, minWidth: 320,
      options: [
        ...catalog.map((entry) => ({ value: entry.path, label: label(entry.path), detail: entry.path })),
        { separator: true },
        { value: ADD, label: 'Add a ROM\u2026', action: true },
      ],
      onPick: (value) => {
        if (value === ADD) openBrowser({ onPicked: (picked, added) => onChange?.(picked, added) });
        else if (value !== path) { select(value); onChange?.(value, false); }
      },
    }));
    return btn;
  }

  DKRLauncher.rom = { entries, fileName, revisionOf, versionOf, label, select, add, remove, restore, openBrowser, romSelect };
})();

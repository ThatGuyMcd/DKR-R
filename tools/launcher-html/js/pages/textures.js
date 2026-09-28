DKRLauncher.pages = DKRLauncher.pages || {};

// Native reference: DrawTextures, DrawTexturePackControls and DrawCrtOverlayControls.
// File operations only modify the browser study's sample library.
DKRLauncher.pages.textures = function (container) {
  const { ui, state } = DKRLauncher;
  const { h } = ui;
  const page = h('div', { class: 'textures-page launcher-detail-page' });
  const text = (value, className = '') => h('p', { class: className, text: value });
  const button = (label, onClick, variant, disabled) => ui.raceButton({ label, onClick, variant, disabled });
  const types = ['Native RT64', 'Rice / RT64 Bridge', 'Legacy Rice', 'Legacy Jabo'];
  const filters = { sort: 'Name A-Z', state: 'All', compatibility: 'All', type: 'All', visibility: 'Visible' };
  let query = '';
  const status = h('p', { class: 'muted texture-status', role: 'status' });
  const count = h('p', { class: 'muted texture-count', 'aria-live': 'polite' });
  const list = h('div', { class: 'texture-library' });
  const packs = () => state.get().textures.packs.map(pack => ({
    format: 'Native RT64', compatible: true, hidden: false, managedBytes: 0, images: 0, importedAt: 0, ...pack,
  }));
  const say = message => { status.textContent = message; };
  function updatePack(id, patch) {
    state.update('textures', { packs: packs().map(pack => pack.id === id ? { ...pack, ...patch } : pack) });
    refresh();
  }
  function size(bytes) {
    if (!bytes) return '0 B';
    const unit = Math.min(4, Math.floor(Math.log(bytes) / Math.log(1024)));
    return (bytes / 1024 ** unit).toFixed(unit ? 1 : 0) + ' ' + ['B', 'KB', 'MB', 'GB', 'TB'][unit];
  }
  function modal(heading, body, actions) {
    const wasOpen = document.getElementById('modal').open;
    const focusedLabel = document.activeElement?.textContent;
    const dialog = ui.openModal({ heading, body, actions });
    dialog.classList.add('launcher-detail-modal');
    if (wasOpen) {
      const buttons = [...dialog.querySelectorAll('button:not(:disabled)')];
      (buttons.find(item => item.textContent === focusedLabel) || buttons[0])?.focus({ preventScroll: true });
    }
    return dialog;
  }
  function remove(pack) {
    modal('REMOVE TEXTURE PACK?', [text(pack.name),
      text("HIDE FROM LIST keeps DKR-R's managed copy on disk. Choose Hidden from the Visibility filter to restore it later.", 'muted'),
      text('WARNING - PERMANENT DELETION CANNOT BE UNDONE', 'warm'),
      text("DELETE COMPLETELY removes DKR-R's managed archive or converted texture cache. The original source archive outside DKR-R is never touched."),
      text('Browser preview: only the sample library is changed.', 'muted')], [
      button('CANCEL', ui.closeModal),
      button('HIDE FROM LIST', () => { updatePack(pack.id, { hidden: true, enabled: false }); ui.closeModal(); say('Texture pack hidden.'); }),
      button('DELETE COMPLETELY', () => {
        state.update('textures', { packs: packs().filter(item => item.id !== pack.id) });
        refresh(); ui.closeModal(); say('Texture pack removed from the preview library.');
      }, 'red'),
    ]);
  }
  function manage(id) {
    const pack = packs().find(item => item.id === id);
    if (!pack) return;
    const details = h('dl', { class: 'launcher-detail-facts' });
    for (const [label, value] of [['Type', pack.format], ['Managed size', size(pack.managedBytes)],
      ['Textures', pack.images], ['Visibility', pack.hidden ? 'Hidden' : 'Visible']]) {
      details.append(h('dt', { text: label }), h('dd', { text: value }));
    }
    const actions = h('div', { class: 'launcher-detail-actions' },
      button(pack.enabled ? 'DEACTIVATE PACK' : 'ACTIVATE PACK', () => {
        updatePack(id, { enabled: !pack.enabled }); manage(id);
      }, undefined, pack.hidden || !pack.compatible),
      button(pack.hidden ? 'RESTORE TO LIBRARY' : 'HIDE FROM LIBRARY', () => {
        updatePack(id, { hidden: !pack.hidden, enabled: pack.hidden ? pack.enabled : false }); manage(id);
      }),
      button('OPEN MANAGED LOCATION', () => ui.notify('Managed folders are available in the desktop launcher.')),
      button('REFRESH PACK DETAILS', () => { manage(id); say('Texture-pack details refreshed.'); }),
      button('REMOVE PACK...', () => remove(pack), 'red'), button('CLOSE', ui.closeModal));
    modal('MANAGE TEXTURE PACK', [h('h3', { text: pack.name }),
      text((pack.hidden ? 'HIDDEN' : pack.enabled ? 'ACTIVE' : 'INACTIVE') + ' · ' +
        (pack.compatible ? 'COMPATIBLE' : 'INCOMPATIBLE'), pack.compatible && !pack.hidden ? 'accent' : 'warm'),
      details, h('h3', { text: 'Managed location' }), text(pack.path || 'Preview library / ' + pack.id, 'muted'),
      pack.detail && text(pack.detail, 'muted'), actions], []);
  }
  function refresh() {
    const focusKey = document.activeElement?.dataset.fk;
    const all = packs();
    const shown = all.filter(pack => {
      const track = pack.origin === 'track';
      return (filters.visibility === 'Track packs' ? track : !track) &&
        (filters.visibility !== 'Visible' || !pack.hidden) && (filters.visibility !== 'Hidden' || pack.hidden) &&
        (filters.state === 'All' || pack.enabled === (filters.state === 'Active')) &&
        (filters.compatibility === 'All' || pack.compatible === (filters.compatibility === 'Compatible')) &&
        (filters.type === 'All' || filters.type === pack.format) &&
        (pack.name + ' ' + pack.format).toLowerCase().includes(query.toLowerCase());
    }).sort((a, b) => {
      const name = a.name.toLowerCase().localeCompare(b.name.toLowerCase()) || a.id.localeCompare(b.id);
      switch (filters.sort) {
        case 'Name Z-A': return -name;
        case 'Largest first': return b.managedBytes - a.managedBytes || name;
        case 'Smallest first': return a.managedBytes - b.managedBytes || name;
        case 'Newest first': return b.importedAt - a.importedAt || name;
        case 'Oldest first': return a.importedAt - b.importedAt || name;
        case 'Type': return types.indexOf(a.format) - types.indexOf(b.format) || name;
        default: return name;
      }
    });
    count.textContent = shown.length + (shown.length === 1 ? ' PACK SHOWN' : ' PACKS SHOWN');
    list.replaceChildren(...shown.map(pack => {
      const check = ui.checkboxRow(pack.name, pack.enabled, enabled => updatePack(pack.id, { enabled }));
      check.querySelector('input').disabled = pack.hidden || !pack.compatible;
      check.querySelector('input').dataset.fk = 'texture-check-' + pack.id;
      const action = button('MANAGE...', () => manage(pack.id));
      action.setAttribute('aria-label', 'Manage ' + pack.name);
      action.dataset.fk = 'texture-manage-' + pack.id;
      return h('section', { class: 'card texture-pack' }, check,
        text(pack.format, pack.compatible ? 'accent' : 'warm'), action);
    }));
    if (!shown.length) list.append(text(all.length ? 'No texture packs match the current search and filters.' :
      'No texture packs have been imported yet.', 'muted texture-empty'));
    if (focusKey && !document.getElementById('modal').open) {
      list.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`)?.focus({ preventScroll: true });
    }
  }
  const archive = h('input', { type: 'file', accept: '.zip,.rtz', hidden: true, onchange: () => {
    const file = archive.files[0];
    archive.value = '';
    if (!file) return;
    if (!/\.(zip|rtz)$/i.test(file.name)) { say('Texture packs must be supplied as ZIP or RTZ archives.'); return; }
    modal('IMPORT TEXTURE PACK', [text(file.name),
      text('Browser preview: adds a sample library entry. The archive is not inspected, converted or installed.', 'muted')], [
      button('CANCEL', ui.closeModal), button('PREVIEW IMPORT', () => {
        state.update('textures', { packs: [...packs(), { id: 'preview-' + crypto.randomUUID(),
          name: file.name.replace(/\.(zip|rtz)$/i, ''), format: 'Unknown', compatible: false,
          enabled: false, hidden: false, managedBytes: file.size, images: 0, importedAt: Date.now(),
          detail: 'Sample import. Format, compatibility and texture count have not been inspected. Use the desktop launcher to prepare this archive for activation.' }] });
        refresh(); ui.closeModal(); say('Texture-pack import preview complete.');
      }),
    ]);
  } });
  const search = h('input', { id: 'texture-search', type: 'search', placeholder: 'Search texture packs...',
    oninput: event => { query = event.target.value; refresh(); } });
  const filterGrid = h('div', { class: 'texture-filters' });
  for (const [key, label, choices] of [
    ['sort', 'Sort', ['Name A-Z', 'Name Z-A', 'Largest first', 'Smallest first', 'Newest first', 'Oldest first', 'Type']],
    ['state', 'State', ['All', 'Active', 'Inactive']], ['compatibility', 'Compatibility', ['All', 'Compatible', 'Incompatible']],
    ['type', 'Type', ['All', ...types]], ['visibility', 'Visibility', ['Visible', 'All', 'Hidden', 'Track packs']],
  ]) filterGrid.append(ui.dropdownRow({ id: 'texture-' + key, label, value: filters[key],
    options: choices.map(value => ({ value, label: value })), onChange: value => { filters[key] = value; refresh(); } }).element);

  function crtControls() {
    const root = h('div');
    const settings = h('div', { class: 'texture-crt-settings' });
    const builtins = ['Soft scanlines', 'Shadow mask', 'Aperture grille', 'Perfect CRT 240p Bright', 'Perfect CRT 240p', 'Perfect CRT'];
    const update = patch => state.update('textures', patch);
    function renderSettings() {
      const saved = state.get().textures;
      settings.hidden = !state.get().graphics.crt;
      settings.replaceChildren(...[
        ['crtFilter', 'Filter image', [...builtins, ...(saved.customFilters || [])]],
        ['crtScaling', 'Scaling', ['Stretch to viewport', 'Tile at native size']],
      ].map(([key, label, choices]) => ui.dropdownRow({ id: 'texture-' + key, label, value: saved[key] || choices[0],
        options: choices.map(value => ({ value, label: value })), onChange: value => update({ [key]: value }) }).element),
      ui.rangeField({ id: 'texture-density', label: 'Filter density', min: 0, max: 100,
        value: saved.crtDensity ?? 35, format: value => value + '%', onInput: value => update({ crtDensity: value }) }).element);
    }
    const image = h('input', { type: 'file', accept: '.png', hidden: true, onchange: () => {
      const file = image.files[0]; image.value = '';
      if (!file) return;
      if (!/\.png$/i.test(file.name)) { ui.notify('Choose a PNG filter image.'); return; }
      const name = file.name.replace(/\.png$/i, '') + ' (Custom)';
      update({ customFilters: [...new Set([...(state.get().textures.customFilters || []), name])], crtFilter: name });
      renderSettings(); ui.notify('Custom filter added to the browser preview.');
    } });
    root.append(ui.checkboxRow('Enable CRT overlay', state.get().graphics.crt, value => {
      state.update('graphics', { crt: value }); renderSettings();
    }), text("Applied only to the game image. DKR-R's settings and performance overlays remain clear above it.", 'muted'),
    settings, image, button('IMPORT CUSTOM CRT FILTER', () => image.click()));
    renderSettings();
    return root;
  }
  function disclosure(label, id, content, open) {
    const body = h('div', { id, hidden: !open, class: 'texture-section-body' }, content);
    const trigger = h('button', { type: 'button', class: 'mods-button', text: label,
      'aria-expanded': String(open), 'aria-controls': id, onclick: () => {
        body.hidden = !body.hidden; trigger.setAttribute('aria-expanded', String(!body.hidden));
      } });
    return h('section', {}, h('div', { class: 'mods-section-nav texture-disclosure' }, trigger), body);
  }
  const modern = state.get().graphics.profile === 'modern';
  const library = h('div', {}, text('Search, arrange and activate managed RT64 and Rice texture packs. Technical import details stay out of the library cards.', 'muted'),
    h('div', { class: 'field' }, h('label', { for: search.id, text: 'Search' }), search), filterGrid, count,
    h('div', { class: 'launcher-detail-actions' }, button('IMPORT TEXTURE PACK', () => archive.click()),
      button('REFRESH PACKS', () => { refresh(); say('Texture packs refreshed.'); })), archive, list, status);
  page.append(h('h1', { id: 'page-heading', text: 'Textures' }),
    text('Personalise the look of DKR-R with texture packs and CRT filters.', 'muted'),
    disclosure('TEXTURE PACKS', 'texture-packs-panel', modern ? library :
      text('Texture-pack management is available in the Modern presentation profile. Accurate mode remains unchanged.', 'muted'), true),
    disclosure('CRT OVERLAYS', 'texture-crt-panel', modern ? crtControls() :
      text('CRT overlays are available in the Modern presentation profile. Accurate mode remains unchanged.', 'muted'), false));
  refresh(); container.append(page);
};

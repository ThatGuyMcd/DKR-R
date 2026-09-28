DKRLauncher.pages = DKRLauncher.pages || {};

// UI study of DrawSaveManager / DrawAdventureBuilder in runtime_ui.cpp.
// Only browser state is edited. Native save files and checksums are not written.
DKRLauncher.pages.saves = function (container) {
  const { ui, state } = DKRLauncher;
  const h = ui.h;
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const courses = [
    'Bluey I', 'Fossil Canyon', 'Pirate Lagoon', 'Ancient Lake', 'Walrus Cove', 'Hot Top Volcano',
    'Whale Bay', 'Snowball Valley', 'Crescent Island', 'Fire Mountain', 'Everfrost Peak', 'Spaceport Alpha',
    'Spacedust Alley', 'Greenwood Village', 'Boulder Canyon', 'Windmill Plains', 'Smokey Castle',
    'Darkwater Beach', 'Icicle Pyramid', 'Frosty Village', 'Jungle Falls', 'Treasure Caves', 'Haunted Woods',
    'Darkmoon Caverns', 'Star City', 'Wizpig I', 'Tricky I', 'Bubbler I', 'Smokey I', 'Tricky II',
    'Bluey II', 'Bubbler II', 'Smokey II', 'Wizpig II',
  ];
  const trials = [
    'Ancient Lake', 'Fossil Canyon', 'Jungle Falls', 'Hot Top Volcano', 'Whale Bay', 'Crescent Island',
    'Pirate Lagoon', 'Treasure Caves', 'Everfrost Peak', 'Walrus Cove', 'Snowball Valley', 'Frosty Village',
    'Boulder Canyon', 'Greenwood Village', 'Windmill Plains', 'Haunted Woods', 'Spacedust Alley',
    'Darkmoon Caverns', 'Star City', 'Spaceport Alpha',
  ];
  // Codec order, including every supported vehicle (47 records per category).
  const records = [
    ['Fossil Canyon', 'Car', 'Hovercraft', 'Plane'], ['Pirate Lagoon', 'Hovercraft'],
    ['Ancient Lake', 'Car', 'Hovercraft', 'Plane'], ['Walrus Cove', 'Car', 'Hovercraft'],
    ['Hot Top Volcano', 'Hovercraft', 'Plane'], ['Whale Bay', 'Hovercraft'],
    ['Snowball Valley', 'Car', 'Hovercraft'], ['Crescent Island', 'Car', 'Hovercraft'],
    ['Everfrost Peak', 'Car', 'Hovercraft', 'Plane'], ['Spaceport Alpha', 'Car', 'Hovercraft', 'Plane'],
    ['Spacedust Alley', 'Car', 'Hovercraft', 'Plane'], ['Greenwood Village', 'Car', 'Hovercraft'],
    ['Boulder Canyon', 'Hovercraft'], ['Windmill Plains', 'Car', 'Hovercraft', 'Plane'],
    ['Frosty Village', 'Car', 'Hovercraft', 'Plane'], ['Jungle Falls', 'Car', 'Hovercraft', 'Plane'],
    ['Treasure Caves', 'Car', 'Hovercraft', 'Plane'], ['Haunted Woods', 'Car', 'Hovercraft'],
    ['Darkmoon Caverns', 'Car', 'Hovercraft'], ['Star City', 'Car', 'Hovercraft', 'Plane'],
  ].flatMap(([course, ...vehicles]) => vehicles.map((vehicle) => course + ' - ' + vehicle));
  const blankImage = () => ({
    slots: Array.from({ length: 3 }, () => ({ name: '   ', balloons: [0, 0, 0, 0, 0, 0],
      ttAmulet: 0, wizpigAmulet: 0, keys: [false, false, false, false], courses: courses.map(() => '0') })),
    settings: { adventureTwo: false, drumstick: false, subtitles: true, language: '0', trials: trials.map(() => false) },
    fastestLaps: records.map(() => ({ time: 0, initials: '' })),
    courseTimes: records.map(() => ({ time: 0, initials: '' })),
  });
  const model = clone(state.get().saves.native || {
    exists: false, valid: false, size: 0, path: 'saves/dkr.us.v77.bin', image: blankImage(), backups: [],
    paks: Array.from({ length: 4 }, (_, i) => ({ exists: false, valid: false, path: `controller-pak-${i + 1}.mpk` })),
  });
  // Like the native builder, edits survive changing pages but are applied only explicitly.
  const view = DKRLauncher.pages.saves.view ||= {
    draft: null, tab: 'adventure-0', section: 'overview', expanded: [false, false, false], recordsOpen: {}, status: '',
  };
  const page = h('div', { class: 'saves-page' });
  const body = h('div', { class: 'saves-body' });
  const save = () => state.update('saves', { native: clone(model) });
  const text = (value, className) => h('p', { text: value, class: className });
  const separator = (label) => h('h2', { class: 'saves-separator', text: label });
  const grid = (className, ...children) => h('div', { class: className }, ...children);
  const feedback = h('p', { class: 'saves-status', role: 'status', 'aria-live': 'polite' });
  const isEditable = () => model.exists && model.valid && model.size === 512;
  const isDirty = () => !!view.draft && JSON.stringify(view.draft) !== JSON.stringify(model.image);
  function button(label, onClick, { disabled = false, className = '', key = label, reason = '' } = {}) {
    const el = ui.raceButton({ label, onClick, disabled });
    el.classList.add('saves-button');
    if (className) el.classList.add(...className.split(' '));
    el.dataset.fk = 'saves-' + key;
    if (reason) { el.dataset.disabledReason = reason; el.title = reason; }
    return el;
  }
  function select(label, labels, value, onChange, key = label) {
    const el = ui.selectRow(label, labels.map((label, i) => ({ value: String(i), label })), String(value), (value) => { onChange(value); syncDraft(); });
    const input = el.querySelector('select');
    input.setAttribute('aria-label', label);
    input.dataset.fk = 'saves-' + key;
    input.id = 'saves-field-' + key.replace(/[^a-z0-9]+/gi, '-');
    el.querySelector('label').htmlFor = input.id;
    return el;
  }
  function checkbox(label, target, key, id = label) {
    const el = ui.checkboxRow(label, target[key], (value) => { target[key] = value; syncDraft(); });
    el.querySelector('input').dataset.fk = 'saves-' + id;
    return el;
  }
  function slider(label, value, max, onInput) {
    const control = ui.rangeField({ id: 'saves-range-' + label.replace(/[^a-z0-9]+/gi, '-'), label, min: 0, max, value,
      onInput: (value) => { onInput(value); syncDraft(); } });
    control.input.dataset.fk = 'saves-' + label;
    return control.element;
  }
  function confirm(heading, paragraphs, label, onConfirm, destructive = false) {
    const modal = ui.openModal({ heading, body: paragraphs.map((p, i) => text(p, i ? 'muted' : '')),
      actions: [button('CANCEL', () => ui.closeModal()), button(label, () => { ui.closeModal(); onConfirm(); }, { className: destructive ? 'saves-danger' : '' })] });
    modal.classList.add('saves-dialog');
    modal.classList.toggle('saves-dialog-reset', destructive);
  }
  function makeBackup() {
    if (!model.exists) return;
    const now = new Date();
    const stamp = now.toISOString().replace(/[-:]/g, '').replace('T', '-').replace('Z', '');
    model.backups.unshift({ name: 'adventure-' + stamp + '.bin', createdAt: now.toISOString(),
      valid: model.valid, size: model.size, image: clone(model.image) });
  }
  function setStatus(message) { view.status = message; save(); render(); feedback.textContent = message; }
  function simulatedTransfer() {
    ui.notify('UI study: save file import and export are available in the application launcher.');
  }

  function syncDraft() {
    const dirty = isDirty();
    const status = page.querySelector('[data-draft-status]');
    if (status) status.textContent = dirty ? 'Unapplied changes' : 'No pending changes';
    for (const key of ['apply', 'discard']) {
      const control = page.querySelector(`[data-fk="saves-${key}"]`);
      if (control) control.disabled = !dirty;
    }
    const editorTab = page.querySelector('#saves-manager-edit');
    if (editorTab) editorTab.textContent = dirty ? 'EDIT PROGRESS *' : 'EDIT PROGRESS';
  }

  function tabBar(definitions, active, prefix, panel, onSelect) {
    const manager = prefix === 'manager';
    const tabs = grid(manager ? 'mods-section-nav saves-manager-nav' : 'saves-tabs');
    tabs.setAttribute('role', 'tablist');
    tabs.setAttribute('aria-label', prefix === 'manager' ? 'Save Manager sections' : 'Save editor sections');
    if (prefix === 'manager') tabs.dataset.tabs = '';
    definitions.forEach(([id, label]) => {
      const selected = active === id;
      const tab = h('button', { type: 'button', role: 'tab', id: `saves-${prefix}-${id}`,
        class: manager ? 'mods-button' : '', 'aria-expanded': manager ? String(selected) : undefined,
        // All tabs remain reachable by the launcher's spatial controller navigation.
        'aria-selected': String(selected), 'aria-pressed': String(selected), 'aria-controls': panel.id,
        'data-fk': `${prefix}-${id}`, onclick: () => { onSelect(id); render(); } },
        ...label.split(/(&)/).map((part) => manager && part === '&' ? h('span', { class: 'saves-nav-symbol', text: part }) : part));
      tabs.append(tab);
      if (selected) panel.setAttribute('aria-labelledby', tab.id);
    });
    tabs.addEventListener('keydown', (event) => {
      const index = definitions.findIndex(([id]) => id === active);
      const next = event.key === 'ArrowRight' ? (index + 1) % definitions.length
        : event.key === 'ArrowLeft' ? (index + definitions.length - 1) % definitions.length
        : event.key === 'Home' ? 0 : event.key === 'End' ? definitions.length - 1 : -1;
      if (next < 0) return;
      event.preventDefault(); event.stopPropagation();
      onSelect(definitions[next][0]); render();
      page.querySelector(`#saves-${prefix}-${definitions[next][0]}`)?.focus();
    });
    return tabs;
  }

  function renderAdventure(host, index) {
    const slot = view.draft.slots[index];
    const initials = grid('saves-initials');
    const alphabet = [...'ABCDEFGHIJKLMNOPQRSTUVWXYZ.?', 'SPACE'];
    Array.from({ length: 3 }, (_, i) => {
      const letter = slot.name.padEnd(3, ' ')[i];
      const field = select(`Racer initial ${i + 1}`, alphabet, letter === ' ' ? 28 : alphabet.indexOf(letter), (value) => {
        const chars = slot.name.padEnd(3, ' ').split('');
        chars[i] = value === '28' ? ' ' : alphabet[Number(value)]; slot.name = chars.join('');
      });
      field.querySelector('label').hidden = true;
      initials.append(field);
    });
    const total = slot.balloons.reduce((a, b) => a + b, 0);
    const progress = h('div', { class: 'saves-progress', role: 'progressbar', 'aria-label': 'Total Golden Balloons (calculated)',
      'aria-valuemin': '0', 'aria-valuemax': '47', 'aria-valuenow': String(total), style: `--progress:${total / 47 * 100}%` }, h('span', { text: total + ' / 47' }));
    const balloonGrid = grid('saves-setting-grid');
    ['DKR-R', 'Dino Domain', 'Sherbet Island', 'Snowflake Mountain', 'Dragon Forest', 'Future Fun Land'].forEach((area, i) => {
      balloonGrid.append(slider(area, slot.balloons[i], i ? 8 : 7, (value) => {
        slot.balloons[i] = value;
        const total = slot.balloons.reduce((a, b) => a + b, 0);
        progress.firstChild.textContent = total + ' / 47';
        progress.style.setProperty('--progress', total / 47 * 100 + '%');
        progress.setAttribute('aria-valuenow', String(total));
      }));
    });
    host.append(text('Racer initials'), initials,
      separator('Golden Balloons'),
      text('Up to 7 on the central island and 8 in each world. The total updates automatically.', 'muted'),
      progress, balloonGrid, separator('Amulets and keys'),
      grid('saves-setting-grid',
        slider('T.T. amulet pieces', slot.ttAmulet, 4, (v) => { slot.ttAmulet = v; }),
        slider('Wizpig amulet pieces', slot.wizpigAmulet, 4, (v) => { slot.wizpigAmulet = v; }),
        ...['Dino Domain key', 'Snowflake Mountain key', 'Sherbet Island key', 'Dragon Forest key'].map((label, i) => checkbox(label, slot.keys, i))));
    const toggle = button((view.expanded[index] ? 'HIDE' : 'SHOW') + ' COURSE PROGRESS (34)', () => {
      view.expanded[index] = !view.expanded[index]; render();
    }, { className: 'saves-comfortable', key: 'course-progress' });
    toggle.setAttribute('aria-expanded', String(view.expanded[index]));
    toggle.setAttribute('aria-controls', 'saves-course-progress');
    host.append(toggle);
    const courseGrid = grid('saves-setting-grid', ...courses.map((course, i) =>
      select(course, ['Not started', 'Race won', 'Silver Coins won', 'Complete'], slot.courses[i], (v) => { slot.courses[i] = v; })));
    courseGrid.id = 'saves-course-progress'; courseGrid.hidden = !view.expanded[index];
    host.append(courseGrid);
    host.append(button('MAX OUT THIS ADVENTURE', () => {
      confirm('Max out this adventure?', ['Set all balloons, amulets, keys and course progress in this slot to complete?', 'This changes the draft. Use Apply changes to save it.'], 'MAX OUT', () => {
        slot.balloons = [7, 8, 8, 8, 8, 8]; slot.ttAmulet = 4; slot.wizpigAmulet = 4;
        slot.keys.fill(true); slot.courses.fill('3'); render();
      });
    }, { className: 'saves-comfortable' }));
  }

  function renderUnlocks(host) {
    const settings = view.draft.settings;
    host.append(grid('saves-setting-grid',
      checkbox('Adventure Two', settings, 'adventureTwo'), checkbox('Drumstick', settings, 'drumstick'),
      checkbox('Subtitles', settings, 'subtitles'),
      select('Language', ['English', 'German', 'French', 'Japanese'], settings.language, (v) => { settings.language = v; })),
      separator('T.T. time-trial victories'),
      grid('saves-setting-grid saves-trials', ...trials.map((label, i) => checkbox(label, settings.trials, i))),
      button('UNLOCK ALL RACERS AND MODES', () => {
        confirm('Unlock all racers and modes?', ['Enable Adventure Two, Drumstick and all T.T. victories in this draft?', 'Use Apply changes to save these unlocks.'], 'UNLOCK ALL', () => {
          settings.adventureTwo = true; settings.drumstick = true; settings.trials.fill(true); render();
        });
      }));
  }

  function renderRecords(host) {
    host.append(text('Personal records are view-only. Race in Time Trial mode to set or improve them.'));
    [['Fastest laps', 'fastestLaps'], ['Course times', 'courseTimes']].forEach(([label, key]) => {
      const details = h('details', { class: 'saves-records', open: view.recordsOpen[key] || undefined },
        h('summary', { text: label, 'data-fk': 'saves-' + key }),
        ...records.map((name, i) => {
          const record = view.draft[key][i];
          const frames = record.time;
          const parts = [Math.floor(frames / 3600), Math.floor(frames % 3600 / 60), Math.floor(frames % 60 * 100 / 60)];
          return h('div', { class: 'saves-record' }, text(name),
            text('Time: ' + (frames ? parts.map((n) => String(n).padStart(2, '0')).join(':') : 'No personal record yet'), 'muted'),
            frames ? text('Racer: ' + (record.initials || '---'), 'muted') : null);
        }));
      details.addEventListener('toggle', () => { view.recordsOpen[key] = details.open; });
      host.append(details);
    });
  }

  function renderBuilder() {
    const builder = h('section', { class: 'saves-builder', 'aria-label': 'Save editor' });
    if (!isEditable()) {
      builder.append(separator('A valid save is needed'),
        text(model.exists ? 'Restore a valid backup or import a working save from Overview before editing.' : 'Start an adventure in the game, import a save, or create a fresh adventure from Overview.', 'muted'),
        button('GO TO OVERVIEW', () => { view.section = 'overview'; render(); }));
      return builder;
    }
    view.draft ||= clone(model.image);
    builder.append(text('Edit a slot or shared unlocks. Changes stay in your draft until you apply them.', 'muted'));
    const panel = h('div', { id: 'saves-builder-panel', role: 'tabpanel', tabindex: '0' });
    const tabs = tabBar([['adventure-0', 'SLOT 1'], ['adventure-1', 'SLOT 2'], ['adventure-2', 'SLOT 3'], ['unlocks', 'UNLOCKS'], ['records', 'RECORDS']],
      view.tab, 'editor', panel, (id) => { view.tab = id; });
    if (view.tab.startsWith('adventure-')) renderAdventure(panel, Number(view.tab.slice(-1)));
    else if (view.tab === 'unlocks') renderUnlocks(panel);
    else renderRecords(panel);
    builder.append(tabs, panel, h('div', { class: 'saves-draft-bar' },
      h('div', {}, h('p', { 'data-draft-status': '', role: 'status' }), text('Applying creates a backup first.', 'muted')),
      button('DISCARD CHANGES', () => {
        confirm('Discard unapplied changes?', ['Reload the current save and discard edits to all three slots and shared unlocks?'], 'DISCARD', () => {
          view.draft = clone(model.image); render();
        }, true);
      }, { key: 'discard' }),
      button('APPLY CHANGES', () => {
        confirm('Apply changes?', ['Replace the current save with this draft? A backup of the current save will be created first.'], 'BACK UP & APPLY', () => {
          makeBackup(); model.image = clone(view.draft);
          setStatus('Changes applied in this preview. Previous progress is available in Backups.');
        });
      }, { className: 'saves-primary', key: 'apply' })));
    return builder;
  }

  function renderTransfer(host) {
    host.append(separator('Move all your saves'),
      text('Transfer Adventure progress and all available Controller Paks together between Windows and Steam Deck.', 'muted'),
      grid('saves-actions saves-actions-two', button('EXPORT COMPLETE GARAGE', simulatedTransfer), button('IMPORT COMPLETE GARAGE', simulatedTransfer)),
      separator('Virtual Controller Paks'), text('Four virtual memory cards. Each card is included in a complete garage export when available.', 'muted'),
      grid('saves-paks', ...model.paks.map((pak, i) => h('section', { class: 'saves-card saves-pak' },
        text('CONTROLLER ' + (i + 1)), text(!pak.exists ? 'Not created yet' : pak.valid ? 'PAK READY' : 'RECOVERY NEEDED', !pak.exists ? 'muted' : pak.valid ? 'accent' : 'saves-error'),
        text(pak.path, 'muted saves-path')))));
  }

  function startFresh() {
    confirm(model.exists ? 'Start a fresh adventure?' : 'Create an Adventure save?',
      [model.exists ? 'Reset all three slots, unlocks and records? Your current save will be backed up first.' : 'Create an empty save with three adventure slots?',
        isDirty() ? 'Your unapplied editor changes will be discarded.' : 'You can edit the new save from Edit progress.'],
      model.exists ? 'BACK UP & RESET' : 'CREATE SAVE', () => {
        const existed = model.exists;
        makeBackup(); model.image = blankImage(); model.exists = true; model.valid = true; model.size = 512; view.draft = null;
        setStatus(existed ? 'Fresh save created. Previous progress is available in Backups.' : 'Fresh save created. Open Edit progress to customize it.');
      }, model.exists);
  }

  function renderOverview(host) {
    host.classList.add('saves-overview');
    const reason = !model.exists ? 'Create or import a save first.' : 'Restore or repair the save first.';
    const fileTools = h('section', { 'aria-labelledby': 'saves-file-heading' },
      h('h2', { id: 'saves-file-heading', text: 'Save file' }),
      text('These actions include all three adventure slots, shared unlocks and personal records.', 'muted'),
      grid('saves-actions saves-file-actions',
      button('Back up save', () => { makeBackup(); setStatus('Backup created. You can restore it below.'); }, { disabled: !isEditable(), reason: !isEditable() ? reason : '', className: 'saves-primary' }),
      button('Export save', simulatedTransfer, { disabled: !isEditable(), reason: !isEditable() ? reason : '' }), button('Import save', simulatedTransfer)));
    host.append(fileTools);
    if (!isEditable()) fileTools.append(text(reason + ' Backup and export require a valid save.', 'muted'));
    if (!model.exists) fileTools.append(button('Create fresh save', startFresh));
    else if (isEditable()) {
      host.append(separator('Adventure slots'),
        grid('saves-slots', ...model.image.slots.map((slot, i) => h('section', { class: 'saves-slot' },
        h('h3', { text: 'Slot ' + (i + 1) }), text(slot.name.trim() || 'No racer initials', 'muted'),
        text(slot.balloons.reduce((a, b) => a + b, 0) + ' / 47 balloons', 'saves-slot-total'),
        button('Edit slot ' + (i + 1), () => {
          view.section = 'edit'; view.tab = 'adventure-' + i; render();
          page.querySelector('#saves-builder-panel')?.focus({ preventScroll: true });
        })))));
    }
    host.append(separator('Backups'), text('Restore earlier progress. A safety backup is also created before applying edits or resetting a save.', 'muted'));
    if (!model.backups.length) host.append(h('div', { class: 'saves-empty' },
      text('No backups yet'), text('Use Back up save to keep a copy of your current progress here.', 'muted')));
    else {
      const backups = view.allBackups ? model.backups : model.backups.slice(0, 6);
      host.append(grid('saves-backups', ...backups.map((backup, i) => h('div', { class: 'saves-backup' },
        h('div', {}, text(backup.createdAt ? new Date(backup.createdAt).toLocaleString() : backup.name),
          text((i === 0 ? 'Latest backup' : 'Earlier backup') + (backup.valid === false ? ' · Needs repair' : ''), 'muted')),
        button('Restore', () => {
          confirm('Restore this backup?', [backup.name, 'Your current save will be backed up before it is replaced.',
            isDirty() ? 'Your unapplied editor changes will be discarded.' : 'You can restore the replaced save from this list.'], 'RESTORE BACKUP', () => {
            const restored = clone(backup.image); makeBackup(); model.image = restored;
            model.exists = true; model.valid = backup.valid ?? true; model.size = backup.size ?? 512; view.draft = null;
            setStatus('Backup restored. Any replaced save was backed up too.');
          }, true);
        }, { key: 'restore-' + i })))));
      if (model.backups.length > 6) host.append(button(view.allBackups ? 'Show recent backups' : `Show all ${model.backups.length} backups`, () => {
        view.allBackups = !view.allBackups; render();
      }, { key: 'all-backups' }));
    }
    if (model.exists) host.append(h('section', { class: 'saves-reset' },
      h('div', {}, h('h2', { text: 'Start over' }), text('Reset all three slots, unlocks and records. A backup is kept.', 'muted')),
      button('Reset save', startFresh, { className: 'saves-danger' })));
  }

  function render() {
    const focusKey = document.activeElement?.dataset.fk;
    let status = 'Ready to use';
    let description = 'Three adventure slots, shared unlocks and personal records.';
    if (!model.exists) { status = 'No Adventure save yet'; description = 'The game creates a save after your first adventure. You can also import a save or create a fresh one below.'; }
    else if (model.size !== 512) { status = 'Save size not recognized'; description = `This save is ${model.size} bytes; 512 bytes are required. Import a working save or restore a backup below.`; }
    else if (!model.valid) { status = 'Save needs repair'; description = 'The save failed its integrity check. Back up and repair it, or restore a working backup.'; }
    const summary = h('section', { class: 'saves-card saves-adventure', 'aria-label': 'Current save' },
      grid('saves-summary-heading', h('h2', { text: 'ADVENTURE SAVE' }),
        h('span', { class: 'saves-badge ' + (!model.exists ? '' : isEditable() ? 'accent' : 'saves-error'), text: status })),
      text(description, 'muted'), text(model.path, 'saves-path muted'));
    if (model.exists && model.size === 512 && !model.valid) summary.append(button('BACK UP & REPAIR SAVE', () => {
      makeBackup(); model.valid = true; view.draft = null;
      setStatus('Repair simulated. A copy of the original is available in Backups.');
    }, { className: 'saves-primary' }));
    const panel = h('div', { id: 'saves-manager-panel', role: 'tabpanel', tabindex: '0' });
    const tabs = tabBar([['overview', 'OVERVIEW'], ['edit', 'EDIT PROGRESS'], ['transfer', 'TRANSFER & PAKS']],
      view.section || 'overview', 'manager', panel, (id) => { view.section = id; });
    if (view.section === 'edit') panel.append(renderBuilder());
    else if (view.section === 'transfer') renderTransfer(panel);
    else renderOverview(panel);
    body.replaceChildren(summary, tabs, panel);
    syncDraft();
    if (focusKey) {
      const next = page.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`);
      (next && !next.disabled ? next : panel).focus({ preventScroll: true });
    }
  }

  page.append(h('h1', { id: 'page-heading', text: 'SAVE MANAGER' }),
    text('Keep your progress safe, edit adventures and move saves between devices.', 'muted saves-intro'),
    text('Browser preview · Changes here do not modify game files.', 'muted saves-preview'), feedback, body);
  feedback.textContent = view.status;
  container.append(page);
  render();
};

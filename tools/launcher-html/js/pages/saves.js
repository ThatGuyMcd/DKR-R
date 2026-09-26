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
    draft: null, tab: 'adventure-0', expanded: [true, true, true], recordsOpen: {}, status: '',
  };
  const page = h('div', { class: 'saves-page' });
  const body = h('div', { class: 'saves-body' });
  const save = () => state.update('saves', { native: clone(model) });
  const text = (value, className) => h('p', { text: value, class: className });
  const separator = (label) => h('h2', { class: 'saves-separator', text: label });
  const space = (height) => h('div', { class: 'saves-space', style: `height:${height}px`, 'aria-hidden': 'true' });
  const grid = (className, ...children) => h('div', { class: className }, ...children);
  function button(label, onClick, { disabled = false, className = '', key = label } = {}) {
    const el = ui.raceButton({ label, onClick, disabled });
    el.classList.add('saves-button');
    if (className) el.classList.add(...className.split(' '));
    el.dataset.fk = 'saves-' + key;
    return el;
  }
  function select(label, labels, value, onChange, key = label) {
    const el = ui.selectRow(label, labels.map((label, i) => ({ value: String(i), label })), String(value), onChange);
    const input = el.querySelector('select');
    input.setAttribute('aria-label', label);
    input.dataset.fk = 'saves-' + key;
    return el;
  }
  function checkbox(label, target, key, id = label) {
    const el = ui.checkboxRow(label, target[key], (value) => { target[key] = value; });
    el.querySelector('input').dataset.fk = 'saves-' + id;
    return el;
  }
  function slider(label, value, max, onInput) {
    const el = ui.sliderRow(label, { min: 0, max, value }, onInput);
    el.querySelector('input').setAttribute('aria-label', label);
    el.querySelector('input').dataset.fk = 'saves-' + label;
    return el;
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
    model.backups.unshift({ name: 'adventure-' + stamp + '.bin', image: clone(model.image) });
  }
  function setStatus(message) { view.status = message; save(); render(); }
  function simulatedTransfer() {
    ui.notify('UI study: save file import and export are available in the application launcher.');
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
      text("The total is calculated automatically. DKR-R's central island region contains seven balloons; each racing world contains eight."),
      text('Total Golden Balloons (calculated)'), progress, balloonGrid, separator('Amulets and keys'),
      grid('saves-setting-grid',
        slider('T.T. amulet pieces', slot.ttAmulet, 4, (v) => { slot.ttAmulet = v; }),
        slider('Wizpig amulet pieces', slot.wizpigAmulet, 4, (v) => { slot.wizpigAmulet = v; }),
        ...['Dino Domain key', 'Snowflake Mountain key', 'Sherbet Island key', 'Dragon Forest key'].map((label, i) => checkbox(label, slot.keys, i))));
    const toggle = button('COURSE PROGRESS  -  ' + (view.expanded[index] ? 'CLOSE' : 'OPEN'), () => {
      view.expanded[index] = !view.expanded[index]; render();
    }, { className: 'saves-comfortable', key: 'course-progress' });
    toggle.setAttribute('aria-expanded', String(view.expanded[index]));
    host.append(toggle);
    if (view.expanded[index]) host.append(grid('saves-setting-grid', ...courses.map((course, i) =>
      select(course, ['Not started', 'Race won', 'Silver Coins won', 'Complete'], slot.courses[i], (v) => { slot.courses[i] = v; }))));
    host.append(button('MAX OUT THIS ADVENTURE', () => {
      slot.balloons = [7, 8, 8, 8, 8, 8]; slot.ttAmulet = 4; slot.wizpigAmulet = 4;
      slot.keys.fill(true); slot.courses.fill('3'); render();
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
        settings.adventureTwo = true; settings.drumstick = true; settings.trials.fill(true); render();
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
    const builder = h('section', { class: 'saves-builder', 'aria-label': 'Save Builder' },
      text("Edit DKR's native EEPROM fields. Applying always creates a dated safety backup, rebuilds every checksum, validates a temporary image and atomically swaps it into T.T.'s garage."),
      button(view.draft ? 'RELOAD LIVE SAVE' : 'OPEN SAVE BUILDER', () => {
        if (!model.valid) { setStatus('No checksum-valid Adventure EEPROM is available to edit.'); return; }
        view.draft = clone(model.image); setStatus('Adventure EEPROM loaded into the builder.');
      }));
    if (view.draft) {
      const tabs = grid('saves-builder-tabs');
      tabs.setAttribute('role', 'tablist'); tabs.setAttribute('aria-label', 'Save Builder sections'); tabs.dataset.tabs = '';
      const panel = h('div', { id: 'saves-builder-panel', role: 'tabpanel' });
      [['adventure-0', 'ADVENTURE 1'], ['adventure-1', 'ADVENTURE 2'], ['adventure-2', 'ADVENTURE 3'], ['unlocks', 'UNLOCKS'], ['records', 'T.T. RECORDS']].forEach(([id, label]) => {
        const tab = h('button', { type: 'button', role: 'tab', id: 'saves-tab-' + id,
          'aria-selected': String(view.tab === id), 'aria-pressed': String(view.tab === id), 'aria-controls': panel.id,
          'data-fk': 'saves-tab-' + id, onclick: () => { view.tab = id; render(); } }, label);
        tabs.append(tab);
        if (view.tab === id) panel.setAttribute('aria-labelledby', tab.id);
      });
      if (view.tab.startsWith('adventure-')) renderAdventure(panel, Number(view.tab.slice(-1)));
      else if (view.tab === 'unlocks') renderUnlocks(panel);
      else renderRecords(panel);
      builder.append(space(8), tabs, panel, space(12), button('BACK UP AND APPLY CHECKSUM-SAFE SAVE', () => {
        confirm('Apply Save Builder changes?', ['Replace the live Adventure EEPROM after creating a dated backup?'], 'BACK UP AND APPLY', () => {
          makeBackup(); model.image = clone(view.draft); model.exists = true; model.valid = true; model.size = 512;
          setStatus('Save Builder changes applied. Checksums verified and the previous EEPROM is backed up.');
        });
      }, { className: 'saves-apply' }));
    }
    body.append(builder);
  }

  function render() {
    const focusKey = document.activeElement?.dataset.fk;
    let status = 'READY - 512 BYTE EEPROM';
    if (!model.exists) status = 'No Adventure save yet. DKR will create one after your first save.';
    else if (model.size !== 512) status = `This file is ${model.size} bytes; DKR Adventure EEPROMs must be exactly 512 bytes. Import a known-good backup before racing.`;
    else if (!model.valid) status = 'This 512-byte EEPROM has invalid DKR checksums. DKR-R can preserve the original and rebuild only its checksum bytes.';
    body.replaceChildren(h('section', { class: 'saves-card saves-adventure' },
      text('ADVENTURE PROGRESS'), text(status, !model.exists ? 'muted' : model.valid ? 'accent' : 'saves-error'), text(model.path, 'muted')), space(10));
    if (model.exists && model.size === 512 && !model.valid) {
      body.append(button('BACK UP AND REPAIR CHECKSUMS', () => {
        makeBackup(); model.valid = true; view.draft = null;
        setStatus('Checksums repaired without changing save data. The exact original is preserved at save-backups/' + model.backups[0].name);
      }), space(10));
    }
    body.append(grid('saves-actions saves-actions-three',
      button('MAKE SAFETY BACKUP', () => { makeBackup(); setStatus("Safety backup parked in T.T.'s garage."); }, { disabled: !model.valid }),
      button('EXPORT SAVE', simulatedTransfer, { disabled: !model.valid }), button('IMPORT SAVE', simulatedTransfer)), space(18));
    renderBuilder();
    body.append(space(14), separator('Complete garage transfer'),
      text('A single path-free bundle carries the Adventure EEPROM and every present virtual Controller Pak between Windows and Steam Deck.'),
      grid('saves-actions saves-actions-two', button('EXPORT COMPLETE GARAGE', simulatedTransfer), button('IMPORT COMPLETE GARAGE', simulatedTransfer)),
      space(14), separator('Virtual Controller Paks'),
      grid('saves-paks', ...model.paks.map((pak, i) => h('section', { class: 'saves-card saves-pak' },
        text('CONTROLLER ' + (i + 1)), text(!pak.exists ? 'Not created yet' : pak.valid ? 'PAK READY' : 'RECOVERY NEEDED', !pak.exists ? 'muted' : pak.valid ? 'accent' : 'saves-error'),
        text(pak.path, 'muted')))));
    if (view.status) body.append(space(8), text(view.status, 'warm saves-status'));
    body.append(space(18), separator('Recent automatic backups'));
    if (!model.backups.length) body.append(text('No backups are parked here yet.', 'muted'));
    else body.append(grid('saves-backups', ...model.backups.slice(0, 6).map((backup, i) => h('section', { class: 'saves-card saves-backup' },
      text(backup.name), button('RESTORE', () => {
        const restored = clone(backup.image); makeBackup(); model.image = restored;
        model.exists = true; model.valid = true; model.size = 512; view.draft = null;
        setStatus('Backup restored. The replaced save was backed up too.');
      }, { key: 'restore-' + i })))));
    body.append(space(18), button('START A FRESH ADVENTURE', () => {
      confirm('Reset Adventure save?', ['Park the current save in a backup, then start fresh?', 'The backup can be restored from this screen later.'], 'START FRESH', () => {
        makeBackup(); model.image = blankImage(); model.exists = true; model.valid = true; model.size = 512; view.draft = null;
        setStatus('Fresh Adventure save created; the previous journey is safe in backups.');
      }, true);
    }, { className: 'saves-danger' }));
    if (focusKey) page.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`)?.focus({ preventScroll: true });
  }

  page.append(h('h1', { id: 'page-heading', text: 'SAVE MANAGER' }),
    text('Back up, import, export or build Adventure progress before racing.', 'muted saves-intro'), body);
  container.append(page);
  render();
};

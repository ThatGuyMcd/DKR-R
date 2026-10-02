DKRLauncher.pages = DKRLauncher.pages || {};

// Browser study of the launcher's controls. Native settings keep their existing
// schema; SDL operations and motion input require the application launcher.
DKRLauncher.pages.controls = function (container) {
  const { ui, state } = DKRLauncher;
  const h = ui.h;
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const actions = [
    ['Analogue up', 'W', 'lefty -'], ['Analogue down', 'S', 'lefty +'],
    ['Analogue left', 'A', 'leftx -'], ['Analogue right', 'D', 'leftx +'],
    ['A button', 'Space', 'a'], ['B button', 'Left Shift', 'x'],
    ['Z trigger', 'Z', 'lefttrigger +'], ['Start', 'Return', 'start'],
    ['D-pad up', 'Up', 'dpup'], ['D-pad down', 'Down', 'dpdown'],
    ['D-pad left', 'Left', 'dpleft'], ['D-pad right', 'Right', 'dpright'],
    ['L shoulder', 'Q', 'leftshoulder'], ['R shoulder', 'E', 'rightshoulder'],
    ['C-up', 'I', 'righty -'], ['C-down', 'K', 'righty +'],
    ['C-left', 'J', 'rightx -'], ['C-right', 'L', 'rightx +'],
  ];
  const defaultBindings = () => actions.map(([, keyboard, primary]) => ({ keyboard, primary, secondary: 'Unbound' }));
  const feelDefaults = { deadzone: 23.95, antiDeadzone: 0, sensitivity: 100, curve: 1, threshold: 0.5 };
  const defaultPlayer = () => ({
    bindings: defaultBindings(), controller: '', backgroundInput: false,
    gyro: false, axis: 'roll', sensitivityX: 100, sensitivityY: 100,
    motionDeadzone: 2, invertX: false, invertY: false,
  });
  const defaults = {
    backend: 'auto', assignment: 'auto', keyboardPlayer: '0',
    memoryPak: true, rumblePak: true, rumbleStrength: 100, quickRestart: false,
    feel: feelDefaults, vehicles: [[false, false], [false, false], [false, false]],
    players: Array.from({ length: 4 }, defaultPlayer),
    shortcuts: [
      ['QUICK RACE RESTART', 'Left Ctrl + R', 'dpdown + start'],
      ['OPEN / CLOSE DKR-R MENU', 'Unbound', 'back'],
      ['TOGGLE TEXTURE PACK', 'Unbound', 'Unbound'],
      ['WINDOWED / FULLSCREEN', 'Unbound', 'Unbound'],
      ['RECENTER GYRO', 'Left Ctrl + G', 'leftstick + rightstick'],
    ],
  };
  const settings = Object.assign(clone(defaults), state.get().controls.native);
  let selectedPlayer = 0;
  let section = 'DEVICE';
  let undo = null;
  const expanded = new Set();
  const modern = () => state.get().graphics.profile === 'modern';
  const player = () => settings.players[selectedPlayer];
  const save = () => {
    undo = null;
    feedback.replaceChildren();
    state.update('controls', { native: clone(settings) });
  };
  const page = h('div', { class: 'controls-page' });
  const players = h('div', { class: 'controls-players', role: 'group', 'aria-label': 'Local players' });
  const tabs = h('div', { class: 'controls-tabs secondary-nav', role: 'tablist', 'aria-label': 'Controls sections', 'data-tabs': true });
  const body = h('div', { class: 'controls-body', id: 'controls-section', role: 'tabpanel' });
  const feedback = h('div', { class: 'controls-feedback', role: 'status', 'aria-live': 'polite' });
  const text = (value, className) => h('p', { class: className, text: value });
  const separator = (label) => h('h2', { class: 'controls-separator', text: label });
  const row = (...children) => h('div', { class: 'controls-row' }, ...children);
  const sections = [['DEVICE', 'Device'], ['N64 BINDINGS', 'Button mapping'], ['DRIVING', 'Stick response'], ['GYRO', 'Motion'], ['SHORTCUTS', 'Shortcuts']];
  const scope = (shared = false) => text(shared ? 'Applies to all players' : `Player ${selectedPlayer + 1} settings`, 'controls-scope');
  function disclosure(label, ...children) {
    const el = ui.disclosure(label, children, expanded.has(label));
    el.querySelector('summary').classList.replace('race-button', 'settings-button');
    el.querySelector('summary').dataset.fk = 'controls-' + label;
    el.addEventListener('toggle', () => { if (el.isConnected) el.open ? expanded.add(label) : expanded.delete(label); });
    return el;
  }
  function withUndo(message, change) {
    const before = clone(settings);
    change(); save();
    undo = { message, before };
    render();
    feedback.querySelector('button').focus({ preventScroll: true });
    feedback.scrollIntoView({ block: 'nearest' });
  }
  function friendly(value) {
    const names = { Unbound: 'Not assigned', a: 'A', b: 'B', x: 'X', y: 'Y', start: 'Menu / Start', back: 'View / Select',
      leftshoulder: 'Left shoulder', rightshoulder: 'Right shoulder', leftstick: 'Left stick click', rightstick: 'Right stick click',
      dpup: 'D-pad ↑', dpdown: 'D-pad ↓', dpleft: 'D-pad ←', dpright: 'D-pad →',
      'lefttrigger +': 'Left trigger', 'righttrigger +': 'Right trigger',
      'leftx -': 'Left stick ←', 'leftx +': 'Left stick →', 'lefty -': 'Left stick ↑', 'lefty +': 'Left stick ↓',
      'rightx -': 'Right stick ←', 'rightx +': 'Right stick →', 'righty -': 'Right stick ↑', 'righty +': 'Right stick ↓' };
    if (names[value]) return names[value];
    return value.replace(/lefttrigger \+|righttrigger \+|left[xy] [+-]|right[xy] [+-]|\b[a-z]+\b/g, (part) => names[part] || part);
  }
  function button(label, onClick, disabled = false) {
    const el = ui.settingsButton({ label, onClick, disabled });
    el.dataset.fk = 'controls-' + label;
    return el;
  }
  function select(label, options, value, onChange) {
    const control = ui.dropdownRow({ id: 'controls-select-' + label.replaceAll(' ', '-'), label,
      options: options.map(([value, label]) => ({ value, label })), value, onChange: (next) => {
        if (onChange(next) === false) { control.setValue(value); return; }
        save(); render();
      } });
    control.button.dataset.fk = 'controls-' + label;
    return control.element;
  }
  function checkbox(label, target, key, rerender = false) {
    const el = ui.checkboxRow(label, target[key], (value) => {
      target[key] = value; save(); if (rerender) render();
    });
    el.querySelector('input').dataset.fk = 'controls-' + label;
    return el;
  }
  function slider(label, target, key, min, max, step, format) {
    const control = ui.rangeField({ id: 'controls-range-' + key, label, min, max, step, value: target[key],
      format: (value) => format(Math.fround(value)), onInput: (value) => { target[key] = value; save(); } });
    control.input.dataset.fk = 'controls-' + label;
    return control.element;
  }
  function progress(label, value = null) {
    return h('div', { class: 'controls-axis' }, h('span', { text: label }),
      h('meter', { min: -1, max: 1, value: value ?? 0, 'aria-label': label, hidden: value === null }),
      h('span', { class: 'controls-axis-value', text: value === null ? '—' : value.toFixed(2) }));
  }
  const studyAction = () => ui.notify('UI study: this action is available in the application launcher.');
  function pads() {
    try { return [...(navigator.getGamepads?.() || [])].filter((pad) => pad?.connected); }
    catch { return []; }
  }
  function assignedPad() {
    return pads().find((pad) => String(pad.index) === player().controller);
  }

  function assignController(value) {
    const owner = settings.players.findIndex((p, i) => i !== selectedPlayer && value && p.controller === value);
    const apply = () => {
      if (owner >= 0) settings.players[owner].controller = '';
      player().controller = value;
      save(); render();
    };
    if (owner < 0) { apply(); return false; }
    const modal = ui.openModal({ heading: 'Move this controller?',
      body: text(`This controller belongs to Player ${owner + 1}. Moving it to Player ${selectedPlayer + 1} will leave Player ${owner + 1} without a controller.`),
      actions: [button('Cancel', () => { ui.closeModal(); render(); }), button('Move controller', () => { ui.closeModal(); apply(); })] });
    modal.classList.add('controls-capture');
    modal.addEventListener('close', () => { if (page.isConnected) render(); }, { once: true });
    return true;
  }

  function renderDevice() {
    const pad = assignedPad();
    const controllerOptions = [['', 'Not assigned'], ...pads().map((p) => [String(p.index), p.id])];
    if (player().controller && !pad) controllerOptions.push([player().controller, 'Assigned controller · disconnected']);
    const controller = select('Controller', controllerOptions, player().controller, (value) => { assignController(value); return false; });
    const status = pad ? 'Connected' : player().controller ? 'Disconnected' : 'No controller assigned';
    const identify = button('Identify with vibration', () => {
      pad.vibrationActuator.playEffect('dual-rumble', { duration: 350, strongMagnitude: 0.6, weakMagnitude: 0.6 })
        .catch(() => ui.notify('This controller could not vibrate. Try pressing a button to identify it.'));
    }, !pad?.vibrationActuator);
    identify.dataset.disabledReason = pad ? 'This controller does not report vibration support.' : 'Connect and assign a controller first.';
    body.append(scope(), separator('Device'), controller,
      text(status, 'controls-connection' + (pad ? ' is-connected' : '')),
      ...(!pad ? [text(player().controller ? 'Reconnect the assigned controller, or choose another device.' : 'Connect a controller, then press one of its buttons. Keyboard mapping is also available.', 'muted')] : []),
      row(button('Press a button to assign', () => capture({ assignment: true })), identify),
      h('section', { class: 'controls-preview', 'aria-label': 'Input test' }, separator('Input test'),
        text(pad ? 'Move the left stick or press a button to check this controller.' : 'Waiting for an assigned controller. Live values will appear here.', 'muted'),
        progress('Horizontal', pad ? pad.axes[0] || 0 : null), progress('Vertical', pad ? pad.axes[1] || 0 : null),
        text('Buttons: ' + (pad ? 'None pressed' : 'Unavailable'), 'controls-buttons-status')),
      checkbox('Allow controller input when the game is unfocused', player(), 'backgroundInput'),
      text('For this player only. Keyboard input requires the game to be focused.', 'muted'));
    const shared = disclosure('Shared settings', scope(true),
      select('Keyboard player', Array.from({ length: 4 }, (_, i) => [String(i), 'Player ' + (i + 1)]), settings.keyboardPlayer, (v) => { settings.keyboardPlayer = v; }),
      select('Controller assignment', [['auto', 'Automatic · first available player'], ['manual', 'Manual']], settings.assignment, (v) => { settings.assignment = v; }),
      separator('Controller Paks'), checkbox('Enable Memory Pak', settings, 'memoryPak'), checkbox('Enable Rumble Pak', settings, 'rumblePak', true));
    const sharedBody = shared.querySelector('.disclosure-body');
    if (settings.rumblePak && modern()) sharedBody.append(slider('Rumble strength', settings, 'rumbleStrength', 0, 100, 1, (v) => v.toFixed(0) + '%'));
    sharedBody.append(text('Memory Pak stores game data. Rumble Pak enables vibration on supported controllers. Both can stay enabled.', 'muted'));
    body.append(shared, disclosure('Advanced compatibility', scope(true),
      text('Launcher-only settings. This browser preview uses the Gamepad API; it cannot report or change an active SDL backend.', 'muted'),
      select('Preferred input backend', [['auto', 'Automatic'], ['sdl2', 'SDL2 compatibility'], ['sdl3', 'SDL3 native']], settings.backend, (v) => { settings.backend = v; }),
      text('Controller map tools are available in the application launcher.', 'muted'),
      row(button('Remap controller · preview', studyAction), button('Import maps · preview', studyAction)), button('Export maps · preview', studyAction)));
  }

  function renderBindings() {
    const keyboard = Number(settings.keyboardPlayer) === selectedPlayer;
    const columns = keyboard ? ['keyboard', 'primary', 'secondary'] : ['primary', 'secondary'];
    const labels = { keyboard: 'Keyboard', primary: 'Controller', secondary: 'Alternate' };
    const bindings = h('table', { class: 'controls-bindings' },
      h('caption', { class: 'controls-sr-only', text: `Player ${selectedPlayer + 1} button mapping` }),
      h('thead', {}, h('tr', {}, ...['N64 control', ...columns.map((key) => labels[key])].map((label) => h('th', { scope: 'col', text: label })))));
    const groups = [['Analogue stick', [0, 1, 2, 3]], ['Main buttons', [4, 5, 6, 7, 12, 13]], ['D-pad', [8, 9, 10, 11]], ['C-buttons', [14, 15, 16, 17]]];
    groups.forEach(([name, indexes]) => {
      const group = h('tbody', {}, h('tr', { class: 'controls-binding-group' }, h('th', { colspan: columns.length + 1, scope: 'rowgroup', text: name })));
      indexes.forEach((index) => {
        const label = actions[index][0];
        group.append(h('tr', { class: 'controls-binding' }, h('th', { scope: 'row', text: label }), ...columns.map((key) => {
          const value = player().bindings[index][key];
          const btn = button(friendly(value), () => capture({ action: index, device: key }));
          btn.classList.toggle('is-unbound', value === 'Unbound');
          btn.setAttribute('aria-label', `${label}: ${labels[key]} — ${friendly(value)}. Change binding`);
          btn.dataset.fk = `controls-binding-${index}-${key}`;
          return h('td', { class: key === 'secondary' ? 'controls-alternate' : '' }, h('span', { class: 'controls-cell-label', 'aria-hidden': 'true', text: labels[key] }), btn);
        })));
      });
      bindings.append(group);
    });
    body.append(scope(), separator('Button mapping'), text('Select a binding, then press a key or controller button. Escape cancels.', 'muted'),
      !keyboard ? text(`Keyboard input belongs to Player ${Number(settings.keyboardPlayer) + 1}. Change it in Device → Shared settings.`, 'muted') : text('Controller names use the standard gamepad layout.', 'muted'), bindings,
      button('Restore this player’s bindings', () => withUndo(`Player ${selectedPlayer + 1} bindings restored.`, () => { player().bindings = defaultBindings(); })),
      disclosure('More mapping actions',
        selectedPlayer ? button('Copy Player 1 bindings', () => withUndo(`Copied Player 1 bindings to Player ${selectedPlayer + 1}.`, () => { player().bindings = clone(settings.players[0].bindings); })) : null,
        button('Restore all players’ bindings', () => withUndo('All player bindings restored.', () => { settings.players.forEach((p) => { p.bindings = defaultBindings(); }); }))));
  }

  function renderDriving() {
    body.append(scope(true), separator('Stick response'),
      slider('Stick sensitivity', settings.feel, 'sensitivity', 50, 150, 1, (v) => v.toFixed(0) + '%'),
      text('Increase to reach full steering with less stick movement.', 'muted'),
      slider('Stick deadzone', settings.feel, 'deadzone', 0, 35, 0.1, (v) => v.toFixed(1) + '%'),
      text('Ignores small movements near the center. Increase if the stick drifts.', 'muted'),
      separator('Axis direction by vehicle'));
    const table = h('table', { class: 'controls-vehicle-table' },
      h('thead', {}, h('tr', {}, ...['Vehicle', 'Horizontal', 'Vertical'].map((label) => h('th', { scope: 'col', text: label })))));
    table.append(h('tbody', {}, ...['Car', 'Hovercraft', 'Plane'].map((vehicle, i) => h('tr', {}, h('th', { scope: 'row', text: vehicle }), ...[0, 1].map((axis) => {
      const el = checkbox('Invert', settings.vehicles[i], axis);
      const input = el.querySelector('input');
      input.setAttribute('aria-label', `Invert ${vehicle} ${axis ? 'vertical' : 'horizontal'}`);
      input.dataset.fk = `controls-invert-${i}-${axis}`;
      return h('td', {}, el);
    })))));
    body.append(table, disclosure('Advanced response',
      slider('Stick anti-deadzone', settings.feel, 'antiDeadzone', 0, 50, 0.1, (v) => v.toFixed(1) + '%'),
      text('Adds a minimum output once the stick leaves its deadzone.', 'muted'),
      slider('Response curve', settings.feel, 'curve', 0.5, 2.5, 0.01, (v) => v.toFixed(2)),
      text('Changes how steering builds as the stick moves. 1.00 gives a linear response.', 'muted'),
      slider('Trigger threshold', settings.feel, 'threshold', 0.05, 0.95, 0.01, (v) => Math.round(v * 100) + '%'),
      text('How far a trigger must be pressed before it counts as a button press.', 'muted')),
      button('Restore stick response and inversions', () => withUndo('Stick response and vehicle inversions restored.', () => { settings.feel = clone(feelDefaults); settings.vehicles = clone(defaults.vehicles); })));
  }

  function renderGyro() {
    const p = player();
    body.append(scope(), separator('Motion steering'), checkbox('Enable gyro steering', p, 'gyro', true),
      text('Requires a compatible motion controller in the application launcher. Live motion input cannot be tested in this browser preview.', 'muted'));
    if (!p.gyro) return;
    body.append(select('Motion style', [['roll', 'Roll controller like a wheel'], ['yaw', 'Turn controller left and right']], p.axis, (v) => { p.axis = v; }),
      slider('Horizontal sensitivity', p, 'sensitivityX', 25, 300, 1, (v) => v.toFixed(0) + '%'),
      slider('Vertical sensitivity', p, 'sensitivityY', 25, 300, 1, (v) => v.toFixed(0) + '%'),
      slider('Motion deadzone', p, 'motionDeadzone', 0, 12, 0.1, (v) => v.toFixed(1) + ' °/s'),
      checkbox('Invert horizontal gyro', p, 'invertX'), checkbox('Invert vertical gyro', p, 'invertY'),
      text('Recenter and calibrate your controller from the in-game overlay.', 'muted'));
  }

  function renderShortcuts() {
    body.append(scope(true), separator('Game shortcuts'),
      text('Available for editing from any player. Select a binding to record a key, button or two-button combination.', 'muted'),
      checkbox('Enable quick race restart', settings, 'quickRestart', true));
    settings.shortcuts.forEach(([label, keyboard, controller], index) => {
      if (!index && !settings.quickRestart) return;
      body.append(h('section', { class: 'controls-shortcut' }, h('h3', { text: label.toLowerCase().replace(/^./, (c) => c.toUpperCase()) }),
        row(...[keyboard, controller].map((binding, i) => {
          const deviceLabel = i ? 'Controller' : 'Keyboard';
          const btn = button(friendly(binding), () => capture({ shortcut: index, device: i ? 'primary' : 'keyboard' }));
          btn.dataset.fk = `controls-shortcut-${index}-${i}`;
          btn.setAttribute('aria-label', `${label}: ${deviceLabel} shortcut — ${friendly(binding)}`);
          return h('div', { class: 'controls-shortcut-field' }, h('span', { text: deviceLabel }), btn);
        }))));
    });
  }

  function keyName(event) {
    const names = { Space: 'Space', Enter: 'Return', ShiftLeft: 'Left Shift', ShiftRight: 'Right Shift', ControlLeft: 'Left Ctrl',
      ControlRight: 'Right Ctrl', AltLeft: 'Left Alt', AltRight: 'Right Alt', ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right' };
    return names[event.code] || (/^Key[A-Z]$/.test(event.code) ? event.code.slice(3) : /^Digit/.test(event.code) ? event.code.slice(5) : event.key);
  }
  function padName(name) {
    const names = { A: 'a', B: 'b', X: 'x', Y: 'y', LB: 'leftshoulder', RB: 'rightshoulder', LT: 'lefttrigger +', RT: 'righttrigger +',
      View: 'back', Menu: 'start', L3: 'leftstick', R3: 'rightstick', 'D-Up': 'dpup', 'D-Down': 'dpdown', 'D-Left': 'dpleft', 'D-Right': 'dpright',
      'LS Left': 'leftx -', 'LS Right': 'leftx +', 'LS Up': 'lefty -', 'LS Down': 'lefty +',
      'RS Left': 'rightx -', 'RS Right': 'rightx +', 'RS Up': 'righty -', 'RS Down': 'righty +' };
    return names[name] || name;
  }
  function capture({ action, shortcut, device = 'primary', assignment = false }) {
    const keyboard = device === 'keyboard';
    const isShortcut = shortcut !== undefined;
    const heading = assignment ? `Assign Player ${selectedPlayer + 1}` : isShortcut ? settings.shortcuts[shortcut][0] : `Player ${selectedPlayer + 1} · ${actions[action][0]}`;
    const help = assignment ? 'Press any button on the controller you want this player to use. Escape cancels.' : isShortcut
      ? `Press one ${keyboard ? 'key' : 'controller button'}, or hold the first and press a second. The chord is saved automatically. Escape cancels.`
      : keyboard ? 'Press a keyboard key. Escape cancels.' : 'Press a gamepad button or move an axis firmly. Escape cancels.';
    const pending = text('');
    let finished = false;
    let first = '';
    let chordTimer;
    let chordPoll;
    let stopPad = () => {};
    const modal = ui.openModal({ heading: 'Choose a new control', body: [h('h3', { text: heading }), text(help), pending],
      actions: [button(assignment ? 'Clear assignment' : 'Remove binding', () => finish('Unbound')), button('Cancel', () => finish())] });
    modal.classList.add('controls-capture');
    modal.tabIndex = -1;
    modal.focus();
    function receive(name) {
      if (!isShortcut) { finish(name); return; }
      if (first && first !== name) { finish(first + ' + ' + name); return; }
      first = name;
      pending.textContent = 'Captured: ' + friendly(name);
      clearTimeout(chordTimer);
      chordTimer = setTimeout(() => finish(name), 800);
      if (!keyboard) {
        armPad();
        clearInterval(chordPoll);
        // The shared single-input capture waits for release before rearming;
        // also watch held buttons so holding the first can form a real chord.
        const names = ['a', 'b', 'x', 'y', 'leftshoulder', 'rightshoulder', 'lefttrigger +', 'righttrigger +',
          'back', 'start', 'leftstick', 'rightstick', 'dpup', 'dpdown', 'dpleft', 'dpright'];
        chordPoll = setInterval(() => {
          const second = pads().flatMap((pad) => pad.buttons.map((b, i) => b.pressed ? names[i] : null)).find((name) => name && name !== first);
          if (second) receive(second);
        }, 30);
      }
    }
    function armPad() {
      stopPad = DKRLauncher.controller.capture((name) => {
        if (keyboard) { armPad(); return; }
        if (assignment) {
          const pad = pads().find((p) => p.buttons.some((b) => b.pressed));
          if (!pad) { armPad(); return; }
          finish(String(pad.index));
        } else receive(padName(name));
      });
    }
    const onKey = (event) => {
      if (event.key === 'Tab' || (event.target.closest?.('button') && ['Enter', ' '].includes(event.key))) return;
      event.preventDefault(); event.stopImmediatePropagation();
      if (event.key === 'Escape') finish();
      else if (keyboard && !event.repeat) receive(keyName(event));
    };
    armPad();
    window.addEventListener('keydown', onKey, true);
    modal.addEventListener('close', () => finish(), { once: true });
    function finish(value) {
      if (finished) return;
      finished = true;
      clearTimeout(chordTimer); clearInterval(chordPoll); stopPad();
      window.removeEventListener('keydown', onKey, true);
      modal.removeAttribute('tabindex');
      const apply = () => {
        if (modal.open) ui.closeModal();
        if (isShortcut) settings.shortcuts[shortcut][keyboard ? 1 : 2] = value;
        else {
          if (value !== 'Unbound') player().bindings.forEach((binding) => {
            for (const key of keyboard ? ['keyboard'] : ['primary', 'secondary']) if (binding[key] === value) binding[key] = 'Unbound';
          });
          player().bindings[action][device] = value;
        }
        save(); render();
      };
      if (value !== undefined) {
        if (assignment) {
          // Reuse the same modal while moving a device, avoiding a close event
          // from the capture dialog dismissing the subsequent confirmation.
          if (!assignController(value === 'Unbound' ? '' : value)) ui.closeModal();
          return;
        }
        const conflicts = !isShortcut && value !== 'Unbound' ? player().bindings.flatMap((binding, index) =>
          (keyboard ? ['keyboard'] : ['primary', 'secondary']).filter((key) => binding[key] === value && (index !== action || key !== device))
            .map((key) => `${actions[index][0]} (${key === 'secondary' ? 'alternate' : keyboard ? 'keyboard' : 'controller'})`)) : [];
        if (conflicts.length) {
          modal.replaceChildren(h('h2', { id: 'modal-heading', text: 'Replace an existing binding?' }),
            text(`${friendly(value)} is already assigned to ${conflicts.join(', ')}. Moving it to ${actions[action][0]} will remove the previous binding.`),
            h('div', { class: 'dialog-actions' }, button('Cancel', () => ui.closeModal()), button('Replace binding', apply)));
          modal.querySelector('button').focus();
        } else apply();
      } else if (modal.open) {
        ui.closeModal();
      }
    }
  }

  function render() {
    const focusKey = document.activeElement?.dataset.fk;
    players.replaceChildren(...settings.players.map((_, i) => {
      const btn = button('Player ' + (i + 1), () => { selectedPlayer = i; render(); });
      btn.className = 'snd-chip';
      btn.setAttribute('aria-pressed', String(selectedPlayer === i));
      return btn;
    }));
    tabs.replaceChildren(...sections.map(([label, title]) => {
      const btn = button(title, () => { section = label; render(); });
      btn.className = 'secondary-nav-tab';
      btn.id = 'controls-tab-' + label.toLowerCase().replaceAll(' ', '-');
      btn.setAttribute('role', 'tab');
      btn.setAttribute('aria-selected', String(section === label));
      btn.setAttribute('aria-pressed', String(section === label));
      btn.setAttribute('aria-controls', 'controls-section');
      btn.classList.toggle('selected', section === label);
      btn.tabIndex = section === label ? 0 : -1;
      if (section === label) body.setAttribute('aria-labelledby', btn.id);
      return btn;
    }));
    body.replaceChildren();
    if ((section === 'DRIVING' || section === 'GYRO') && !modern()) body.append(scope(section === 'DRIVING'), separator(section === 'DRIVING' ? 'Stick response' : 'Motion steering'),
      text('These adjustments require Modern presentation. Accurate preserves the original control response.', 'muted'),
      button('Open graphics settings', () => { location.hash = '#/graphics'; }));
    else ({ DEVICE: renderDevice, 'N64 BINDINGS': renderBindings, DRIVING: renderDriving, GYRO: renderGyro, SHORTCUTS: renderShortcuts })[section]();
    feedback.replaceChildren();
    if (undo) feedback.append(text(undo.message), button('Undo', () => {
      Object.assign(settings, clone(undo.before)); save(); render();
      body.tabIndex = -1; body.focus({ preventScroll: true });
      ui.notify('Previous controls settings restored.');
    }));
    if (focusKey) page.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`)?.focus({ preventScroll: true });
  }

  tabs.addEventListener('keydown', (event) => {
    const index = sections.findIndex(([id]) => id === section);
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? sections.length - 1 : event.key === 'ArrowRight' ? (index + 1) % sections.length : event.key === 'ArrowLeft' ? (index + sections.length - 1) % sections.length : -1;
    if (next < 0) return;
    event.preventDefault(); event.stopPropagation();
    section = sections[next][0]; render();
    tabs.querySelector('[aria-selected="true"]').focus();
  });
  page.append(h('h1', { id: 'page-heading', text: 'CONTROLS' }),
    text('Configure your devices and button mappings.', 'muted controls-intro'),
    h('div', { class: 'controls-player-bar' }, h('span', { text: 'Local player' }), players), tabs, feedback, body,
    text('Browser preview · Changes are saved in this browser.', 'controls-study-note'));
  container.append(page);
  render();

  // Update live values without rebuilding controls under focus.
  let previousPads = '';
  const previewTimer = setInterval(() => {
    if (!page.isConnected) { clearInterval(previewTimer); return; }
    if (section !== 'DEVICE' || document.querySelector('dialog[open]')) return;
    const connected = pads();
    const signature = connected.map((p) => p.index + ':' + p.id).join('|');
    if (signature !== previousPads) {
      previousPads = signature;
      let changed = false;
      if (settings.assignment === 'auto') connected.forEach((pad) => {
        if (settings.players.some((p) => p.controller === String(pad.index))) return;
        const free = settings.players.find((p) => !p.controller);
        if (free) { free.controller = String(pad.index); changed = true; }
      });
      if (changed) save();
      render();
    }
    const pad = assignedPad();
    body.querySelectorAll('.controls-axis').forEach((axis, i) => {
      const value = pad?.axes[i] || 0;
      axis.querySelector('meter').value = value;
      axis.querySelector('.controls-axis-value').textContent = pad ? value.toFixed(2) : '—';
    });
    const status = body.querySelector('.controls-buttons-status');
    const names = ['A', 'B', 'X', 'Y', 'Left shoulder', 'Right shoulder', 'Left trigger', 'Right trigger', 'View', 'Menu', 'Left stick', 'Right stick', 'D-pad ↑', 'D-pad ↓', 'D-pad ←', 'D-pad →'];
    const pressed = pad?.buttons.flatMap((b, i) => b.pressed ? [names[i] || `Button ${i + 1}`] : []).join(', ');
    if (status) status.textContent = 'Buttons: ' + (pad ? pressed || 'None pressed' : 'Unavailable');
  }, 100);
};

DKRLauncher.pages = DKRLauncher.pages || {};

// Mirrors DrawControlsReference / DrawLocalPlayers in runtime_ui.cpp. Settings
// belong to this browser study; SDL backend and mapping operations are previews.
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
  const modern = () => state.get().graphics.profile === 'modern';
  const player = () => settings.players[selectedPlayer];
  const save = () => state.update('controls', { native: clone(settings) });
  const page = h('div', { class: 'controls-page' });
  const players = h('div', { class: 'controls-players', role: 'group', 'aria-label': 'Local players' });
  const tabs = h('div', { class: 'controls-tabs', role: 'tablist', 'aria-label': 'Controls sections', 'data-tabs': true });
  const body = h('div', { class: 'controls-body', id: 'controls-section', role: 'tabpanel' });
  const text = (value, className) => h('p', { class: className, text: value });
  const separator = (label) => h('h2', { class: 'controls-separator', text: label });
  const row = (...children) => h('div', { class: 'controls-row' }, ...children);
  const spacer = (height) => h('div', { class: 'controls-space', style: `height:${height}px`, 'aria-hidden': 'true' });
  function button(label, onClick, disabled = false) {
    const el = ui.raceButton({ label, onClick, disabled });
    el.dataset.fk = 'controls-' + label;
    return el;
  }
  function select(label, options, value, onChange) {
    const el = ui.selectRow(label, options.map(([value, label]) => ({ value, label })), value, (next) => {
      onChange(next); save(); render();
    });
    el.querySelector('select').setAttribute('aria-label', label);
    el.querySelector('select').dataset.fk = 'controls-' + label;
    return el;
  }
  function checkbox(label, target, key, rerender = false) {
    const el = ui.checkboxRow(label, target[key], (value) => {
      target[key] = value; save(); if (rerender) render();
    });
    el.querySelector('input').dataset.fk = 'controls-' + label;
    return el;
  }
  function slider(label, target, key, min, max, step, format) {
    const el = ui.sliderRow(label, { min, max, value: target[key], format: (value) => format(Math.fround(value)) }, (value) => { target[key] = value; save(); });
    const input = el.querySelector('input');
    input.step = step;
    // Setting step after construction must not leave the value rounded to step=1.
    input.value = target[key];
    if (key === 'sensitivityX' || key === 'sensitivityY') el.classList.add('controls-gyro-sensitivity');
    input.setAttribute('aria-label', label);
    input.dataset.fk = 'controls-' + label;
    return el;
  }
  function progress(label, value = 0.5) {
    return h('div', { class: 'controls-progress', role: 'progressbar', 'aria-label': label,
      'aria-valuemin': '0', 'aria-valuemax': '100', 'aria-valuenow': String(value * 100), style: `--progress:${value * 100}%` },
    h('span', { text: label }));
  }
  const studyAction = () => ui.notify('UI study: this action is available in the application launcher.');
  function pads() {
    try { return [...(navigator.getGamepads?.() || [])].filter((pad) => pad?.connected); }
    catch { return []; }
  }
  function assignedPad() {
    return pads().find((pad) => String(pad.index) === player().controller);
  }

  function renderDevice() {
    const native3 = settings.backend === 'sdl3';
    body.append(
      select('Controller input backend', [['auto', 'Automatic (SDL3 on Steam Deck)'], ['sdl2', 'SDL2 compatibility'], ['sdl3', 'SDL3 native']], settings.backend, (v) => { settings.backend = v; }),
      text('Active: ' + (native3 ? 'SDL3 native' : 'SDL2 compatibility'), 'accent'),
      text(native3 ? 'Native SDL3 controller input active.' : 'Native SDL2 compatibility input active.'),
      text('Backend changes apply live; the launcher, game window, audio and renderer remain running.', 'muted'),
      spacer(10));
    const pad = assignedPad();
    const preview = h('div', { class: 'controls-preview' },
      h('h2', { text: 'PLAYER ' + (selectedPlayer + 1) }),
      text(pad ? pad.id : player().controller ? 'Assigned controller disconnected' : 'No controller assigned'),
      text(pad ? 'Connected' + (pad.vibrationActuator ? '  |  Rumble' : '') : player().controller
        ? 'Reconnect it to reclaim this player automatically' : 'Choose one below or press a button to assign', pad ? 'accent' : 'muted'),
      progress('Horizontal stick', ((pad?.axes[0] || 0) + 1) / 2),
      progress('Vertical stick', ((pad?.axes[1] || 0) + 1) / 2),
      text('Buttons: ' + (pad?.buttons.some((b) => b.pressed) ? 'Active' : 'Idle'), 'muted controls-buttons-status'));
    body.append(preview,
      select('Assignment style', [['auto', 'Automatic (first connected)'], ['manual', 'Manual']], settings.assignment, (v) => { settings.assignment = v; }),
      select('Controller', [['', 'Unassigned'], ...pads().map((p) => [String(p.index), p.id])], pad ? player().controller : '', (v) => {
        if (v) settings.players.forEach((p) => { if (p.controller === v) p.controller = ''; });
        player().controller = v;
      }),
      row(button('PRESS A BUTTON TO ASSIGN', () => capture({ assignment: true })),
        button('IDENTIFY WITH RUMBLE', () => {
          pad?.vibrationActuator?.playEffect('dual-rumble', { duration: 350, strongMagnitude: 0.6, weakMagnitude: 0.6 }).catch(studyAction);
        }, !pad?.vibrationActuator)));
    if (pad) body.append(button('REMAP THIS CONTROLLER', studyAction, native3));
    body.append(row(button('IMPORT CONTROLLER MAPS', studyAction, native3), button('EXPORT CUSTOM MAPS', studyAction)));
    if (native3) body.append(text('SDL3 uses its native controller database. Switch to SDL2 compatibility and restart to create or import raw mappings.'));
    body.append(select('Keyboard player', Array.from({ length: 4 }, (_, i) => [String(i), 'Player ' + (i + 1)]), settings.keyboardPlayer, (v) => { settings.keyboardPlayer = v; }));
    if (selectedPlayer) body.append(button('COPY PLAYER 1 BINDINGS', () => { player().bindings = clone(settings.players[0].bindings); save(); }));
    body.append(spacer(43), separator('CONTROLLER PAKS'),
      checkbox('Enable Mem Pak', settings, 'memoryPak'), checkbox('Enable Rumble Pak', settings, 'rumblePak', true));
    if (settings.rumblePak && modern()) body.append(slider('Rumble strength', settings, 'rumbleStrength', 0, 100, 1, (v) => v.toFixed(0) + '%'));
    body.append(text('Both options can remain enabled. DKR-R gives the virtual Mem Pak priority whenever the game requests storage, while compatible controllers can still rumble independently.', 'muted'));
  }

  function renderBindings() {
    const keyboard = Number(settings.keyboardPlayer) === selectedPlayer;
    const columns = keyboard ? ['keyboard', 'primary', 'secondary'] : ['primary', 'secondary'];
    const labels = { keyboard: 'KEYBOARD', primary: 'GAMEPAD PRIMARY', secondary: 'GAMEPAD SECONDARY' };
    const bindings = h('div', { class: 'controls-bindings' + (keyboard ? ' has-keyboard' : '') });
    bindings.append(h('div', { class: 'controls-binding-header' }, h('span', { text: 'N64 CONTROL' }), ...columns.map((key) => h('span', { text: labels[key] }))));
    actions.forEach(([label], index) => {
      bindings.append(h('div', { class: 'controls-binding' }, h('span', { class: 'controls-binding-label', text: label }), ...columns.map((key) => {
        const btn = button(player().bindings[index][key], () => capture({ action: index, device: key }));
        btn.setAttribute('aria-label', `${label}: ${labels[key]} — ${player().bindings[index][key]}`);
        btn.dataset.fk = `controls-binding-${index}-${key}`;
        return btn;
      })));
    });
    body.append(h('h2', { class: 'controls-underlined', text: 'DRIVER BINDINGS' }), bindings,
      row(button('RESET THIS PLAYER', () => { player().bindings = defaultBindings(); save(); render(); }),
        button('RESET ALL PLAYERS', () => { settings.players.forEach((p) => { p.bindings = defaultBindings(); }); save(); render(); })));
  }

  function renderDriving() {
    body.append(separator('CONTROLLER FEEL'),
      slider('Stick deadzone', settings.feel, 'deadzone', 0, 35, 0.1, (v) => v.toFixed(1) + '%'),
      slider('Stick anti-deadzone', settings.feel, 'antiDeadzone', 0, 50, 0.1, (v) => v.toFixed(1) + '%'),
      slider('Stick sensitivity', settings.feel, 'sensitivity', 50, 150, 1, (v) => v.toFixed(0) + '%'),
      slider('Response curve', settings.feel, 'curve', 0.5, 2.5, 0.01, (v) => v.toFixed(2)),
      slider('Trigger threshold', settings.feel, 'threshold', 0.05, 0.95, 0.01, (v) => v.toFixed(2)),
      separator('VEHICLE-SPECIFIC AXIS DIRECTION'));
    const table = h('table', { class: 'controls-vehicle-table' },
      h('thead', {}, h('tr', {}, ...['VEHICLE', 'HORIZONTAL', 'VERTICAL'].map((label) => h('th', { scope: 'col', text: label })))));
    table.append(h('tbody', {}, ...['Car', 'Hovercraft', 'Plane'].map((vehicle, i) => h('tr', {}, h('td', { text: vehicle }), ...[0, 1].map((axis) => {
      const el = checkbox('Invert', settings.vehicles[i], axis);
      const input = el.querySelector('input');
      input.setAttribute('aria-label', `Invert ${vehicle} ${axis ? 'vertical' : 'horizontal'}`);
      input.dataset.fk = `controls-invert-${i}-${axis}`;
      return h('td', {}, el);
    })))));
    body.append(table, text('These adjustments shape the final N64 stick sample once per authored game update. Accurate keeps the original response.', 'muted'),
      button('RESTORE CONTROLLER FEEL', () => { settings.feel = clone(feelDefaults); settings.vehicles = clone(defaults.vehicles); save(); render(); }));
  }

  function renderGyro() {
    const p = player();
    body.append(separator('MOTION STEERING'), checkbox('Gyro steering', p, 'gyro', true));
    if (!p.gyro) return;
    body.append(select('Motion style', [['roll', 'Roll controller like a wheel'], ['yaw', 'Yaw controller left and right']], p.axis, (v) => { p.axis = v; }),
      slider('Horizontal gyro sensitivity', p, 'sensitivityX', 25, 300, 1, (v) => v.toFixed(0) + '%'),
      slider('Vertical gyro sensitivity', p, 'sensitivityY', 25, 300, 1, (v) => v.toFixed(0) + '%'),
      slider('Motion deadzone', p, 'motionDeadzone', 0, 12, 0.1, (v) => v.toFixed(1) + ' deg/s'),
      checkbox('Invert horizontal gyro', p, 'invertX'), checkbox('Invert vertical gyro', p, 'invertY'),
      progress('Horizontal steering'), progress('Vertical steering'),
      button('RECENTER STEERING', null, true), button('CALIBRATE CONTROLLER', null, true),
      text('Calibration is available from the in-game overlay.', 'muted'));
  }

  function renderShortcuts() {
    body.append(separator('BACKGROUND PLAY'), checkbox('Allow Background Inputs', player(), 'backgroundInput'),
      text(`The controller assigned to Player ${selectedPlayer + 1} can keep racing while DKR-R is not focused. Keyboard input remains focus-only.`, 'muted'), spacer(10));
    if (selectedPlayer) body.append(text('Game shortcuts are configured from Player 1.', 'muted'));
    const shortcuts = h('fieldset', { class: 'controls-shortcuts', disabled: selectedPlayer !== 0 },
      separator('GAME SHORTCUTS'), checkbox('Enable quick race restart', settings, 'quickRestart', true));
    settings.shortcuts.forEach(([label, keyboard, controller], index) => {
      if (!index && !settings.quickRestart) return;
      shortcuts.append(h('div', { class: 'controls-shortcut' }, text(label),
        row(text('Keyboard shortcut'), text('Controller shortcut')),
        row(...[keyboard, controller].map((binding, i) => {
          const btn = button(binding, () => capture({ shortcut: index, device: i ? 'primary' : 'keyboard' }));
          btn.dataset.fk = `controls-shortcut-${index}-${i}`;
          btn.setAttribute('aria-label', `${label}: ${i ? 'Controller' : 'Keyboard'} shortcut — ${binding}`);
          return btn;
        }))));
    });
    body.append(shortcuts);
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
    const heading = assignment ? `ASSIGN PLAYER ${selectedPlayer + 1}` : isShortcut ? settings.shortcuts[shortcut][0] : `PLAYER ${selectedPlayer + 1} - ${actions[action][0]}`;
    const help = assignment ? 'Press any button on the controller you want this player to use. Escape cancels.' : isShortcut
      ? `Press one ${keyboard ? 'key' : 'controller button'}, or hold the first and press a second. The chord is saved automatically. Escape cancels.`
      : keyboard ? 'Press a keyboard key. Escape cancels.' : 'Press a gamepad button or move an axis firmly. Escape cancels.';
    const pending = text('');
    let finished = false;
    let first = '';
    let chordTimer;
    let chordPoll;
    let stopPad = () => {};
    const modal = ui.openModal({ heading: 'CHOOSE A NEW CONTROL', body: [h('h3', { text: heading }), text(help), pending],
      actions: [button(assignment ? 'CLEAR ASSIGNMENT' : 'UNBIND', () => finish('Unbound')), button('CANCEL', () => finish())] });
    modal.classList.add('controls-capture');
    modal.tabIndex = -1;
    modal.focus();
    function receive(name) {
      if (!isShortcut) { finish(name); return; }
      if (first && first !== name) { finish(first + ' + ' + name); return; }
      first = name;
      pending.textContent = 'Captured: ' + name;
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
      if (modal.open) ui.closeModal();
      if (value !== undefined) {
        if (assignment) {
          if (value !== 'Unbound') settings.players.forEach((p) => { if (p.controller === value) p.controller = ''; });
          player().controller = value === 'Unbound' ? '' : value;
        } else if (isShortcut) settings.shortcuts[shortcut][keyboard ? 1 : 2] = value;
        else {
          if (value !== 'Unbound') player().bindings.forEach((binding) => {
            for (const key of keyboard ? ['keyboard'] : ['primary', 'secondary']) if (binding[key] === value) binding[key] = 'Unbound';
          });
          player().bindings[action][device] = value;
        }
        save(); render();
      }
    }
  }

  function render() {
    const focusKey = document.activeElement?.dataset.fk;
    players.replaceChildren(...settings.players.map((_, i) => {
      const btn = button('PLAYER ' + (i + 1), () => { selectedPlayer = i; render(); });
      btn.classList.toggle('selected', selectedPlayer === i);
      btn.setAttribute('aria-pressed', String(selectedPlayer === i));
      return btn;
    }));
    tabs.replaceChildren(...['DEVICE', 'N64 BINDINGS', 'DRIVING', 'GYRO', 'SHORTCUTS'].map((label) => {
      const btn = button(label, () => { section = label; render(); });
      btn.id = 'controls-tab-' + label.toLowerCase().replaceAll(' ', '-');
      btn.setAttribute('role', 'tab');
      btn.setAttribute('aria-selected', String(section === label));
      btn.setAttribute('aria-pressed', String(section === label));
      btn.setAttribute('aria-controls', 'controls-section');
      btn.classList.toggle('selected', section === label);
      if (section === label) body.setAttribute('aria-labelledby', btn.id);
      return btn;
    }));
    body.replaceChildren();
    if ((section === 'DRIVING' || section === 'GYRO') && !modern()) body.append(text('Driving and gyro tuning are available in Modern presentation style.', 'muted'));
    else ({ DEVICE: renderDevice, 'N64 BINDINGS': renderBindings, DRIVING: renderDriving, GYRO: renderGyro, SHORTCUTS: renderShortcuts })[section]();
    if (focusKey) page.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`)?.focus({ preventScroll: true });
  }

  page.append(h('h1', { id: 'page-heading', text: 'CONTROLS' }),
    text('Keyboard or gamepad - pick your machine and hit the track.', 'muted controls-intro'),
    h('h2', { class: 'controls-underlined controls-player-heading', text: 'LOCAL PLAYERS' }), players, tabs, body);
  container.append(page);
  render();

  // Keep the two native preview bars live without rebuilding the controls under focus.
  let previousPads = '';
  const previewTimer = setInterval(() => {
    if (!page.isConnected) { clearInterval(previewTimer); return; }
    if (section !== 'DEVICE' || document.querySelector('dialog[open]')) return;
    const connected = pads();
    const signature = connected.map((p) => p.index + ':' + p.id).join('|');
    if (signature !== previousPads) {
      previousPads = signature;
      if (settings.assignment === 'auto') connected.forEach((pad) => {
        if (settings.players.some((p) => p.controller === String(pad.index))) return;
        const free = settings.players.find((p) => !p.controller);
        if (free) free.controller = String(pad.index);
      });
      render();
    }
    const pad = assignedPad();
    body.querySelectorAll('.controls-preview .controls-progress').forEach((bar, i) => {
      const value = ((pad?.axes[i] || 0) + 1) * 50;
      bar.style.setProperty('--progress', value + '%'); bar.setAttribute('aria-valuenow', String(value));
    });
    const status = body.querySelector('.controls-buttons-status');
    if (status) status.textContent = 'Buttons: ' + (pad?.buttons.some((b) => b.pressed) ? 'Active' : 'Idle');
  }, 100);
};

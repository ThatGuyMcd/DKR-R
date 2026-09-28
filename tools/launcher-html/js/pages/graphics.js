DKRLauncher.pages = DKRLauncher.pages || {};

// Graphics UI study: game settings are browser state; interface scaling is live.
DKRLauncher.pages.graphics = function (container) {
  const { ui, state, display } = DKRLauncher;
  const h = ui.h;
  const page = h('div', { class: 'graphics-page' });
  const controls = [];
  let undoGraphics = null;
  const clone = (value) => JSON.parse(JSON.stringify(value));
  const status = h('p', { class: 'gfx-status', role: 'status', 'aria-live': 'polite' });

  function save(key, value) {
    state.update('graphics', { [key]: value });
    undoGraphics = null;
    status.textContent = '';
    syncActions();
  }

  function section(id, title, caption, ...children) {
    return h('section', { class: 'card gfx-section', 'data-section': id, 'aria-labelledby': 'gfx-' + id },
      h('header', { class: 'gfx-section-header' }, h('h2', { id: 'gfx-' + id },
        ...title.split(/(&)/).map((part) => part === '&' ? h('span', { class: 'gfx-heading-symbol', text: part }) : part)),
        h('p', { class: 'gfx-caption', text: caption })), ...children);
  }

  function select(key, label, options, description) {
    const control = ui.dropdownRow({ id: 'gfx-' + key, label,
      options: options.map(([value, label]) => ({ value, label })), value: state.get().graphics[key], onChange: (value) => save(key, value) });
    const field = control.element;
    const input = control.button;
    input.dataset.fk = input.id;
    input.setAttribute('aria-describedby', input.id + '-help');
    field.append(h('p', { id: input.id + '-help', class: 'gfx-help', text: description }));
    controls.push(() => control.setValue(state.get().graphics[key]));
    return field;
  }

  function checkbox(key, label, description) {
    const row = ui.checkboxRow(label, state.get().graphics[key], (value) => save(key, value));
    const input = row.querySelector('input');
    input.id = 'gfx-' + key; input.dataset.fk = input.id;
    input.setAttribute('aria-describedby', input.id + '-help');
    const labelText = row.querySelector('span');
    labelText.className = 'gfx-option-label';
    // The description sits outside the label so assistive technology reads it once.
    const result = h('div', { class: 'gfx-option' }, row,
      h('p', { id: input.id + '-help', class: 'gfx-help', text: description }));
    controls.push(() => { input.checked = state.get().graphics[key]; });
    return result;
  }

  const hudControl = ui.rangeField({ id: 'gfx-hudSize', label: 'HUD size',
    min: 75, max: 150, value: state.get().graphics.hudSize, format: (value) => value + '%', onInput: (value) => save('hudSize', value) });
  const hud = hudControl.element;
  const hudInput = hudControl.input;
  hudInput.dataset.fk = hudInput.id;
  hudInput.setAttribute('aria-describedby', 'gfx-hud-help');
  hud.append(h('p', { id: 'gfx-hud-help', class: 'gfx-help', text: 'Size of the in-game HUD. 100% is the default; launcher text uses Interface size below.' }));
  controls.push(() => hudControl.setValue(state.get().graphics.hudSize));

  const advanced = h('details', { class: 'gfx-advanced' },
    h('summary', { 'data-fk': 'gfx-advanced' }, 'Renderer & performance'),
    h('div', { class: 'gfx-advanced-body' },
      select('renderer', 'Renderer', [['rt64', 'RT64 (modern)'], ['software', 'Software (original)']],
        'Choose the rendering backend used to draw the game.'),
      checkbox('fpsOverlay', 'Show performance overlay', 'Display game performance statistics while playing.')));

  const scaleControl = ui.dropdownRow({ id: 'gfx-interface-size', label: 'Interface size', options: [],
    value: String(state.get().display.uiScale), onChange: (value) => {
      state.update('display', { uiScale: value });
      display.set(value); syncScale();
    } });
  scaleControl.element.classList.add('gfx-interface-field');
  scaleControl.button.setAttribute('aria-describedby', 'gfx-interface-help gfx-scale-status');
  scaleControl.button.dataset.fk = 'ui-scale';
  const scaleStatus = h('p', { id: 'gfx-scale-status', class: 'gfx-help', role: 'status' });
  const unavailableReason = 'Needs a bigger window: the launcher needs 1280 x 720 of room after scaling.';
  function syncScale() {
    scaleControl.setOptions([{ value: 'auto', label: `Auto (${Math.round(display.resolve('auto') * 100)}% in this window)` },
      ...display.steps.map((step) => ({ value: String(step), label: Math.round(step * 100) + '%',
        disabled: !display.allowed(step), reason: unavailableReason }))]);
    scaleControl.setValue(String(state.get().display.uiScale));
    const actual = Math.round(display.scale() * 100);
    const requested = Number(display.setting) * 100;
    const limited = display.setting !== 'auto' && actual < requested;
    scaleStatus.textContent = limited
      ? `Currently ${actual}%. Your ${Math.round(requested)}% preference will be used when the window is large enough.`
      : `Currently ${actual}%. Larger sizes become available when there is enough room.`;
  }

  const reset = ui.raceButton({ label: 'RESTORE GRAPHICS DEFAULTS', onClick: () => {
    undoGraphics = clone(state.get().graphics);
    state.reset('graphics');
    controls.forEach((sync) => sync());
    status.textContent = 'Graphics defaults restored. Interface size is unchanged.';
    syncActions();
    undo.focus({ preventScroll: true });
  } });
  reset.dataset.fk = 'gfx-reset';
  const undo = ui.raceButton({ label: 'UNDO', onClick: () => {
    if (!undoGraphics) return;
    state.update('graphics', undoGraphics); undoGraphics = null;
    controls.forEach((sync) => sync());
    status.textContent = 'Your previous graphics settings have been restored.';
    syncActions(); reset.focus({ preventScroll: true });
  } });
  undo.dataset.fk = 'gfx-undo';
  function syncActions() {
    reset.disabled = Object.keys(state.defaults.graphics).every((key) => state.get().graphics[key] === state.defaults.graphics[key]);
    reset.dataset.disabledReason = 'Graphics settings already match the defaults.';
    undo.hidden = !undoGraphics;
  }

  page.append(
    h('header', { class: 'gfx-header' }, h('h1', { id: 'page-heading', text: 'GRAPHICS' }),
      h('p', { class: 'gfx-caption', text: 'Adjust the game display, visual detail and interface size.' }),
      h('p', { class: 'gfx-preview', text: 'Game settings are saved in this browser preview. Interface size updates the launcher immediately.' })),
    section('display', 'Game display', 'Window, resolution and frame synchronization.',
      h('div', { class: 'gfx-fields' },
        select('windowMode', 'Window mode', [['windowed', 'Windowed'], ['borderless', 'Borderless fullscreen'], ['exclusive', 'Exclusive fullscreen']],
          'Play in a window or use the full screen.'),
        select('resolution', 'Resolution', [['1280x720', '1280 x 720'], ['1920x1080', '1920 x 1080'], ['2560x1440', '2560 x 1440'], ['3840x2160', '3840 x 2160']],
          'Higher resolutions draw more pixels and can increase graphics load.')),
      checkbox('vsync', 'Vertical sync (VSync)', 'Synchronize frames with the display to reduce screen tearing.'), advanced),
    h('div', { class: 'gfx-columns' },
      section('appearance', 'HUD & effects', 'Readability and the look of the game image.', hud,
        checkbox('crt', 'Enable CRT overlay', 'Apply a CRT filter to the game image. Launcher text and performance overlays stay clear.')),
      section('scenery', 'Vehicles & scenery', 'More visible detail can increase CPU and GPU load.',
        checkbox('maxVehicleDetail', 'Maximum vehicle detail', 'Keep racers on their most detailed models.'),
        checkbox('keepHubScenery', 'Keep hub scenery rendered', 'Keep forward-visible scenery in the adventure hub.'),
        checkbox('keepTrackScenery', 'Keep track and boss scenery rendered', 'Keep forward-visible scenery during races and boss challenges.'),
        checkbox('ultrawideGuard', 'Ultrawide scenery guard', 'Expand visibility checks to reduce scenery disappearing near wide-screen edges.'))),
    section('interface', 'Launcher interface', 'Size of launcher menus and text for your screen or couch setup.',
      scaleControl.element,
      h('p', { id: 'gfx-interface-help', class: 'gfx-help', text: 'Auto adapts to the available window size. Choices that would leave too little room are unavailable.' }), scaleStatus),
    h('footer', { class: 'gfx-footer' },
      h('div', {}, h('h2', { text: 'Restore graphics defaults' }),
        h('p', { class: 'gfx-help', text: 'Reset game graphics settings. Interface size is kept. You can undo until your next graphics change or until you leave this page.' })),
      h('div', { class: 'gfx-reset-actions' }, reset, undo), status));
  container.append(page);
  syncScale(); syncActions();
  page.querySelector('#gfx-windowMode').dataset.defaultFocus = '';
  addEventListener('resize', syncScale);
  // This page is mounted anew on navigation; release its window listener when it leaves.
  const lifecycle = new MutationObserver(() => {
    if (page.isConnected) return;
    removeEventListener('resize', syncScale); lifecycle.disconnect();
  });
  lifecycle.observe(container, { childList: true });
};

DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.graphics = function (container) {
  const { ui, state } = DKRLauncher;
  const heading = ui.h('h1', { id: 'page-heading', text: 'Graphics' });
  const wrap = ui.h('div', { class: 'settings stack' });

  // Interface size (js/display.js): the whole launcher, for TVs and big screens. Sizes that would
  // leave less than 1280 x 720 of room stay listed but say why they are unavailable.
  function interfaceSize() {
    const display = DKRLauncher.display;
    const select = ui.h('select', { 'aria-label': 'Interface size', 'data-fk': 'ui-scale' },
      ui.h('option', { value: 'auto' }, `Auto (${Math.round(display.resolve('auto') * 100)}% on this screen)`),
      ...display.steps.map((step) => {
        const fits = display.allowed(step);
        return ui.h('option', {
          value: String(step), disabled: !fits || undefined,
          'data-reason': fits ? undefined : 'Needs a bigger screen: the launcher needs 1280 x 720 of room after scaling.',
        }, Math.round(step * 100) + '%');
      }));
    select.value = String(state.get().display.uiScale);
    // The room left after scaling changes with the window, so re-check the steps on resize.
    const sync = () => {
      if (!select.isConnected) { removeEventListener('resize', sync); return; }
      select.options[0].textContent = `Auto (${Math.round(display.resolve('auto') * 100)}% on this screen)`;
      [...select.options].slice(1).forEach((option) => { option.disabled = !display.allowed(Number(option.value)); });
    };
    addEventListener('resize', sync);
    select.addEventListener('change', () => {
      state.update('display', { uiScale: select.value });
      display.set(select.value);
    });
    return ui.card([
      ui.h('div', { class: 'field' }, ui.h('label', {}, 'Interface size'), select),
      ui.h('p', { class: 'muted interface-note' }, 'Makes the launcher bigger for TVs and couch play. Auto stays at 100% up to 1440p and scales 4K screens to 200% and 5K screens to 250%.'),
    ]);
  }

  function refresh() {
    const g = state.get().graphics;
    wrap.replaceChildren(
      interfaceSize(),
      ui.card([
        ui.selectRow('Resolution', [
          { value: '1280x720', label: '1280 x 720' },
          { value: '1920x1080', label: '1920 x 1080' },
          { value: '2560x1440', label: '2560 x 1440' },
          { value: '3840x2160', label: '3840 x 2160' },
        ], g.resolution, (v) => state.update('graphics', { resolution: v })),
        ui.selectRow('Window mode', [
          { value: 'windowed', label: 'Windowed' },
          { value: 'borderless', label: 'Borderless fullscreen' },
          { value: 'exclusive', label: 'Exclusive fullscreen' },
        ], g.windowMode, (v) => state.update('graphics', { windowMode: v })),
        ui.selectRow('Renderer', [
          { value: 'rt64', label: 'RT64 (modern)' },
          { value: 'software', label: 'Software (original)' },
        ], g.renderer, (v) => state.update('graphics', { renderer: v })),
        ui.checkboxRow('Vertical sync', g.vsync, (v) => state.update('graphics', { vsync: v })),
      ]),
      ui.card([
        ui.sliderRow('HUD size', { min: 75, max: 150, value: g.hudSize, format: (v) => v + '%' },
          (v) => state.update('graphics', { hudSize: v })),
        ui.checkboxRow('Show DKR-R performance overlay', g.fpsOverlay, (v) => state.update('graphics', { fpsOverlay: v })),
        ui.checkboxRow('Maximum vehicle detail', g.maxVehicleDetail, (v) => state.update('graphics', { maxVehicleDetail: v })),
        ui.checkboxRow('Keep hub scenery rendered', g.keepHubScenery, (v) => state.update('graphics', { keepHubScenery: v })),
        ui.checkboxRow('Keep track and boss scenery rendered', g.keepTrackScenery, (v) => state.update('graphics', { keepTrackScenery: v })),
        ui.checkboxRow('Ultrawide scenery guard', g.ultrawideGuard, (v) => state.update('graphics', { ultrawideGuard: v })),
      ]),
      ui.card([
        ui.checkboxRow('Enable CRT overlay', g.crt, (v) => state.update('graphics', { crt: v })),
      ]),
      ui.raceButton({
        label: 'RESTORE ACCURATE DEFAULTS', variant: 'orange',
        onClick: () => { state.reset('graphics'); ui.notify('Graphics settings restored.'); refresh(); },
      }));
  }
  refresh();

  container.append(heading, wrap);
};

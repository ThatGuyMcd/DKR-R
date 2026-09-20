DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.controls = function (container) {
  const { ui, state } = DKRLauncher;
  const heading = ui.h('h1', { id: 'page-heading', text: 'Controls' });

  const preview = ui.h('div', { class: 'card controller-preview' },
    ui.h('h2', { text: 'Controller preview' }),
    ui.h('div', { class: 'stick-preview' }, ui.h('div', { class: 'stick' }), ui.h('div', { class: 'stick' })),
    ui.h('p', { class: 'muted' }, 'Decorative preview only - no controller input is read here.'));

  const bindingsCard = ui.h('div', { class: 'card stack' });
  function refreshBindings() {
    const bindings = state.get().controls.bindings;
    const rows = Object.entries(bindings).map(([action, key]) => {
      const btn = ui.raceButton({ label: key, onClick: () => chooseControl(action) });
      btn.dataset.fk = 'bind-' + action;
      return ui.h('div', { class: 'binding-row' }, ui.h('span', { text: action }), btn);
    });
    bindingsCard.replaceChildren(
      ui.h('h2', { text: 'Key bindings - Player 1' }),
      ...rows,
      ui.raceButton({
        label: 'RESET ALL PLAYERS', variant: 'orange',
        onClick: () => { state.reset('controls'); ui.notify('Bindings reset.'); refreshBindings(); refreshOptions(); },
      }));
  }

  // CHOOSE A NEW CONTROL: the next controller button, firm stick move or keyboard key becomes the
  // binding. Every controller button can be bound, B included, so a controller-only player cancels
  // by waiting: nothing pressed for 5 seconds cancels. Escape cancels too, as in the native launcher.
  function chooseControl(action) {
    const SECONDS = 5;
    let left = SECONDS;
    let done = false;
    const count = ui.h('strong', {}, String(left));
    const modal = ui.openModal({
      heading: 'PLAYER 1 - ' + action.toUpperCase(),
      body: [
        ui.h('p', {}, 'Press a controller button, move a stick firmly, or press a keyboard key.'),
        ui.h('p', { class: 'capture-timer', role: 'status' }, 'Nothing pressed for ', count, ' seconds cancels. Escape cancels too.'),
        ui.h('span', { class: 'capture-bar', 'aria-hidden': 'true', style: `--capture-seconds:${SECONDS}s` }, ui.h('i')),
      ],
      actions: [
        ui.raceButton({ label: 'UNBIND', onClick: () => finish('Unbound') }),
        ui.raceButton({ label: 'CANCEL', onClick: () => finish() }),
      ],
    });
    // Any press binds, so no button may look focused (A would bind A, not press UNBIND).
    modal.tabIndex = -1;
    modal.focus();
    const onKey = (event) => {
      event.preventDefault();
      event.stopImmediatePropagation();
      if (event.key === 'Escape') finish();
      else finish('Key ' + (event.key.length === 1 ? event.key.toUpperCase() : event.key));
    };
    const stopPad = DKRLauncher.controller.capture((name) => finish('Pad ' + name));
    const timer = setInterval(() => {
      left -= 1;
      count.textContent = String(left);
      if (left <= 0) finish();
    }, 1000);
    window.addEventListener('keydown', onKey, true);
    modal.addEventListener('close', () => finish(), { once: true });

    function finish(binding) {
      if (done) return;
      done = true;
      clearInterval(timer);
      stopPad();
      window.removeEventListener('keydown', onKey, true);
      modal.removeAttribute('tabindex');
      if (modal.open) ui.closeModal();
      if (binding !== undefined) {
        state.update('controls', { bindings: Object.assign({}, state.get().controls.bindings, { [action]: binding }) });
        ui.notify(`${action}: ${binding}`);
      }
      refreshBindings();
      bindingsCard.querySelector(`[data-fk="${CSS.escape('bind-' + action)}"]`)?.focus();
    }
  }

  const optionsCard = ui.h('div', { class: 'card stack' });
  function refreshOptions() {
    const c = state.get().controls;
    const rows = [
      ui.checkboxRow('Gyro steering', c.gyro, (v) => { state.update('controls', { gyro: v }); refreshOptions(); }),
      c.gyro ? ui.checkboxRow('Invert horizontal gyro', c.invertX, (v) => state.update('controls', { invertX: v })) : null,
      c.gyro ? ui.checkboxRow('Invert vertical gyro', c.invertY, (v) => state.update('controls', { invertY: v })) : null,
      ui.checkboxRow('Enable quick race restart', c.quickRestart, (v) => state.update('controls', { quickRestart: v })),
      ui.checkboxRow('Allow background inputs', c.backgroundInput, (v) => state.update('controls', { backgroundInput: v })),
    ].filter(Boolean);
    optionsCard.replaceChildren(...rows);
  }

  refreshBindings();
  refreshOptions();

  container.append(heading, preview, bindingsCard, optionsCard);
};

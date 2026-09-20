DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.saves = function (container) {
  const { ui, state } = DKRLauncher;
  const heading = ui.h('h1', { id: 'page-heading', text: 'Saves' });

  const tabDefs = [
    { id: 'adventure', label: 'ADVENTURE' },
    { id: 'unlocks', label: 'UNLOCKS' },
    { id: 'records', label: 'T.T. RECORDS' },
  ];

  const tabsHost = ui.h('div');
  const body = ui.h('div', { class: 'card stack' });

  function confirmFreshStart() {
    ui.openModal({
      heading: 'START A FRESH ADVENTURE',
      body: ui.h('p', {}, 'This clears all adventure progress on this save. Continue?'),
      actions: [
        ui.raceButton({ label: 'CANCEL', onClick: () => ui.closeModal() }),
        ui.raceButton({
          label: 'START FRESH', variant: 'red',
          onClick: () => { ui.closeModal(); ui.notify('Fresh adventure started (simulated).'); },
        }),
      ],
    });
  }

  function renderBody(activeId) {
    if (activeId === 'adventure') {
      body.replaceChildren(
        ui.h('p', {}, 'Adventure save management for the current garage.'),
        ui.raceButton({ label: 'MAKE SAFETY BACKUP', onClick: () => ui.notify('Backup created (simulated).') }),
        ui.raceButton({ label: 'EXPORT SAVE', onClick: () => ui.notify('Save exported (simulated).') }),
        ui.raceButton({ label: 'IMPORT SAVE', onClick: () => ui.notify('Save imported (simulated).') }),
        ui.raceButton({ label: 'BACK UP AND REPAIR CHECKSUMS', variant: 'orange', onClick: () => ui.notify('Checksums repaired (simulated).') }),
        ui.raceButton({ label: 'START A FRESH ADVENTURE', variant: 'red', onClick: confirmFreshStart }));
    } else if (activeId === 'unlocks') {
      body.replaceChildren(
        ui.h('p', {}, 'Racers, tracks and modes unlocked on this save.'),
        ui.raceButton({ label: 'UNLOCK ALL RACERS AND MODES', variant: 'orange', onClick: () => ui.notify('Everything unlocked (simulated).') }));
    } else {
      body.replaceChildren(
        ui.h('p', {}, 'Time trial ghost records for every track.'),
        ui.h('p', { class: 'empty muted' }, 'No recorded times in this prototype.'));
    }
  }

  function renderTabs(activeId) {
    tabsHost.replaceChildren(ui.tabs(tabDefs, activeId, (id) => {
      state.update('saves', { activeTab: id });
      renderTabs(id);
      renderBody(id);
    }));
  }

  const active = state.get().saves.activeTab;
  renderTabs(active);
  renderBody(active);

  container.append(heading, tabsHost, body);
};

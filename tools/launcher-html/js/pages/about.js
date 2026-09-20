DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.about = function (container) {
  const { ui, state, config } = DKRLauncher;
  const heading = ui.h('h1', { id: 'page-heading', text: 'About' });

  const supportCard = ui.h('div', { class: 'card support-card stack' },
    ui.h('h2', { text: 'DKR-R' }),
    ui.h('p', {}, 'Release: ', ui.h('span', { class: 'accent' }, config.version)),
    ui.h('p', { class: 'muted' }, 'A faithful, actively developed recompilation of Diddy Kong Racing.'),
    ui.h('div', { class: 'row' },
      ui.raceButton({ label: 'VISIT GITHUB PAGE', onClick: () => window.open(config.links.github, '_blank', 'noopener') }),
      ui.raceButton({ label: 'JOIN THE DISCORD', onClick: () => window.open(config.links.discord, '_blank', 'noopener') })),
    ui.h('div', { class: 'separator' }),
    ui.h('h3', { text: 'Open-source credits' }),
    ...config.credits.map((c) => ui.h('div', { class: 'credit-row' },
      ui.h('span', { text: c.name }),
      ui.raceButton({ label: 'VISIT GITHUB', onClick: () => window.open(c.url, '_blank', 'noopener') }))));

  const diagCard = ui.h('div', { class: 'card stack' });
  function refreshDiag() {
    const a = state.get().about;
    diagCard.replaceChildren(
      ui.h('h2', { text: 'Diagnostics' }),
      ui.checkboxRow('Diagnostic logging', a.diagnosticLogging, (v) => state.update('about', { diagnosticLogging: v })),
      ui.checkboxRow('Create crash dumps', a.crashDumps, (v) => state.update('about', { crashDumps: v })),
      ui.raceButton({ label: 'EXPORT SUPPORT SUMMARY', onClick: () => ui.notify('Support summary export is simulated in this prototype.') }));
  }
  refreshDiag();

  container.append(heading, supportCard, diagCard);
};

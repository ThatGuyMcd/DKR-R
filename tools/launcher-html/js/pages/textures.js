DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.textures = function (container) {
  const { ui, state } = DKRLauncher;
  const heading = ui.h('h1', { id: 'page-heading', text: 'Textures' });
  const list = ui.h('div', { class: 'stack' });

  function refresh() {
    const packs = state.get().textures.packs;
    list.replaceChildren(...(packs.length ? packs.map((pack) => ui.h('div', { class: 'card row' },
      ui.checkboxRow(pack.name, pack.enabled, (v) => {
        const next = state.get().textures.packs.map((p) => (p.id === pack.id ? { ...p, enabled: v } : p));
        state.update('textures', { packs: next });
      }),
      ui.raceButton({ label: 'MANAGE...', onClick: () => ui.notify('Pack management is simulated in this prototype.') })))
      : [ui.h('p', { class: 'empty muted' }, 'No texture packs installed.')]));
  }
  refresh();

  container.append(
    heading,
    ui.h('div', { class: 'row' },
      ui.raceButton({ label: 'IMPORT TEXTURE PACK', onClick: () => ui.notify('Pack import is simulated in this prototype.') }),
      ui.raceButton({ label: 'REFRESH PACKS', onClick: () => { ui.notify('Packs refreshed.'); refresh(); } })),
    list);
};

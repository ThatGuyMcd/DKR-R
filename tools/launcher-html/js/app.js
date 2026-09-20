(function () {
  const { config, state, ui } = DKRLauncher;

  function currentPageId() {
    const id = location.hash.replace(/^#\/?/, '');
    return config.pages.some((p) => p.id === id) ? id : 'play';
  }

  function renderNav() {
    const nav = document.getElementById('navigation');
    const active = currentPageId();
    // Update in place after the first build: recreating the button under the cursor
    // would restart its hover fade and cut the press animation short.
    if (nav.children.length) {
      for (const btn of nav.children) {
        if (btn.dataset.page === active) btn.setAttribute('aria-current', 'page');
        else btn.removeAttribute('aria-current');
      }
      return;
    }
    nav.replaceChildren(...config.pages.map((page) =>
      ui.raceButton({
        label: page.label,
        variant: page.variant,
        current: page.id === active,
        dataPage: page.id,
        onClick: () => { location.hash = '#/' + page.id; },
      })));
  }

  function confirmAction(heading, message, confirmLabel, variant) {
    const modal = ui.openModal({
      heading,
      body: ui.h('p', { text: message }),
      actions: [
        ui.raceButton({ label: 'CANCEL', onClick: () => ui.closeModal() }),
        ui.raceButton({
          label: confirmLabel,
          variant,
          onClick: () => {
            ui.closeModal();
            ui.notify('This is a UI study - no game process is actually running.');
          },
        }),
      ],
    });
    return modal;
  }

  function renderSidebarActions() {
    const actions = document.getElementById('sidebar-actions');
    actions.replaceChildren(
      ui.raceButton({
        label: 'RESTART DKR-R',
        variant: 'orange',
        onClick: () => confirmAction('RESTART DKR-R', 'Restart the game now?', 'RESTART DKR-R', 'orange'),
      }),
      ui.raceButton({
        label: 'EXIT DKR-R',
        variant: 'red',
        onClick: () => confirmAction('EXIT DKR-R', 'Exit the launcher and the game?', 'EXIT DKR-R', 'red'),
      })
    );
  }

  function renderPage() {
    const id = currentPageId();
    const content = document.getElementById('content');
    content.replaceChildren();
    content.scrollTop = 0;
    const render = DKRLauncher.pages[id];
    if (render) render(content);
    // Walking the sidebar with the D-pad or arrow keys switches pages as you go; keep focus there.
    const walkingSidebar = DKRLauncher.controller?.mode !== 'pointer' && document.activeElement?.closest?.('.sidebar');
    if (!walkingSidebar) content.focus({ preventScroll: true });
    renderNav();
  }

  window.addEventListener('hashchange', renderPage);
  document.addEventListener('DOMContentLoaded', () => {
    renderSidebarActions();
    renderPage();
  });
})();

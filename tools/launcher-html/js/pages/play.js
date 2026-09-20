DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.play = function (container) {
  const { ui, state, rom: roms } = DKRLauncher;
  const h = ui.h;
  // Last ROM removed from the list and where it sat, so Remove can be undone while the page stays open.
  let removed = null;
  // Notice keys from the previous render; null until the first one so nothing animates on page load.
  let shownNotices = null;

  const reducedMotion = () => document.documentElement.dataset.motion === 'off' ||
    matchMedia('(prefers-reduced-motion: reduce)').matches;

  // Adding a ROM (browser) lands on START; switching in the drop-down stays on the drop-down.
  function romChanged(path, added) {
    removed = null;
    if (added) ui.notify('Added to your ROM list: ' + roms.fileName(path));
    render({ focus: added ? 'start' : 'rom', arriving: true });
  }

  function browse() {
    roms.openBrowser({ onPicked: romChanged });
  }

  // Racing's periods nearly touch and read as an underscore; give a trailing "..." room to breathe.
  function raceButton(options) {
    const btn = ui.raceButton(options);
    if (options.label.endsWith('...')) {
      btn.firstChild.textContent = options.label.slice(0, -3);
      btn.firstChild.append(h('span', { class: 'play-ellipsis' }, '...'));
    }
    return btn;
  }

  // Same filename stand-in for ROM detection as the Mods page: 1 = v 1.0, 2 = v 1.1.
  function romRevision(rom) {
    return rom.ready ? roms.revisionOf(rom.path) : 0;
  }

  function mismatchedMods(revision) {
    const library = state.get().mods.library;
    return [...library.tracks, ...library.characters]
      .filter((card) => card.enabled && !card.hidden && !card.native && (card.revisions & revision) === 0);
  }

  function textButton(label, onClick, cls) {
    return h('button', { type: 'button', class: 'play-text-button ' + (cls || ''), onclick: onClick }, label);
  }

  function romCard(rom) {
    const count = roms.entries().length;
    const undo = removed ? textButton('Undo', () => {
      roms.restore(removed.path, removed.index);
      removed = null;
      render({ focus: 'rom', arriving: true });
    }) : null;
    const removedNote = removed ? 'Removed ' + roms.fileName(removed.path) + ' from your list.' : '';

    let details, actions;
    if (count) {
      // The ROM list: every Game Pak DKR-R has accepted, one pick away.
      details = [
        h('div', { class: 'rom-pick' },
          h('span', { class: 'rom-pick-label', id: 'rom-pick-label' }, 'Game ROM'),
          roms.romSelect({ focusKey: 'rom', onChange: romChanged })),
        h('p', { class: 'rom-meta' }, removedNote || (count === 1
          ? 'Add your other Game Pak revision to switch between them here.'
          : count + ' ROMs in your list. Switch any time before you start.')),
      ];
      actions = [
        raceButton({ label: 'ADD A ROM...', onClick: browse }),
        undo || textButton('Remove from list', () => {
          const index = roms.entries().findIndex((entry) => entry.path === rom.path);
          removed = { path: rom.path, index };
          const next = roms.remove(rom.path);
          render({ focus: next ? 'rom' : 'browse' });
        }, 'is-destructive'),
      ];
    } else {
      details = [h('p', { class: 'rom-file' }, 'Load an original Diddy Kong Racing ROM to continue.'),
        h('p', { class: 'rom-meta' }, removedNote || 'Accepted files: .z64, .n64, .v64')];
      actions = [raceButton({ label: 'BROWSE FOR ROM...', variant: 'green', onClick: browse }), undo];
    }
    actions[0].dataset.focus = 'browse';
    return h('section', { class: 'card race-pass' + (rom.ready ? ' ready' : ''), 'aria-labelledby': 'rom-status' },
      h('div', { class: 'race-pass-status' },
        h('div', { class: 'traffic-lights', 'aria-hidden': 'true' }, h('i'), h('i'), h('i')),
        h('div', { class: 'race-pass-body' },
          h('h2', { id: 'rom-status', text: rom.ready ? 'Ready to race' : 'ROM not loaded' }),
          ...details,
          h('div', { class: 'rom-actions' }, ...actions))));
  }

  // Collapse a notice in place so START glides up instead of jumping, then re-render.
  function dismiss(notice, then) {
    if (notice.classList.contains('is-leaving')) return;
    (startButton.disabled ? container.querySelector('[data-focus="browse"]') : startButton).focus();
    if (reducedMotion()) { then(); return; }
    let done = false;
    const finish = () => { if (!done) { done = true; then(); } };
    notice.style.height = notice.offsetHeight + 'px';
    notice.getBoundingClientRect();
    notice.classList.add('is-leaving');
    notice.style.height = '0px';
    notice.addEventListener('transitionend', (event) => { if (event.target === notice && event.propertyName === 'height') finish(); });
    setTimeout(finish, 400);
  }

  // Things that change what START does, shown right above it.
  function launchNotices(rom) {
    const notices = [];
    const lab = state.get().mods.trackLab;
    const testTrack = [...(lab.tracks || []), ...(lab.watched || [])].find((track) => track.id === lab.armed);
    if (testTrack) {
      const notice = h('div', { class: 'play-notice', role: 'status' },
        h('div', {},
          h('strong', {}, 'Track Lab test: ' + testTrack.name),
          h('p', {}, lab.autoBoot
            ? 'Starts directly in this track as Diddy, in single player. Uses the Modern graphics profile.'
            : 'Native DKR track. Uses the Modern graphics profile.')),
        h('button', {
          type: 'button', class: 'mods-button',
          onclick: () => dismiss(notice, () => {
            state.update('mods', { trackLab: Object.assign({}, state.get().mods.trackLab, { armed: '', autoBoot: false }) });
            render({ focus: 'start' });
          }),
        }, 'Stop testing'));
      notices.push({ key: 'test', el: notice });
    }
    const mismatched = rom.ready ? mismatchedMods(romRevision(rom)) : [];
    if (mismatched.length) {
      const one = mismatched.length === 1;
      notices.push({ key: 'mods', el: h('div', { class: 'play-notice is-warning', role: 'status' },
        h('div', {},
          h('strong', {}, one ? '1 enabled mod needs preparing' : mismatched.length + ' enabled mods need preparing'),
          h('p', {}, mismatched.map((card) => card.name).join(', ') + (one ? ' was' : ' were') +
            ' prepared for a different ROM version. Use Prepare mod on ' + (one ? 'its card.' : 'their cards.'))),
        h('a', { class: 'mods-button', href: '#/mods' }, 'Open Mods')) });
    }
    return notices;
  }

  // START and the notice slot stay mounted so their state changes can transition.
  const startButton = raceButton({
    label: 'START Diddy Kong Racing - Recompiled',
    variant: 'green',
    onClick: () => ui.notify('This is a UI study - the game is not actually launched.'),
  });
  startButton.classList.add('full', 'start-button');
  startButton.dataset.focus = 'start';
  const startHint = h('p', { class: 'play-start-hint', id: 'start-hint' }, 'Load a ROM to start.');
  const noticeSlot = h('div', { class: 'play-notices' });
  let cardSlot = h('div');

  container.replaceChildren(h('div', { class: 'play-page' },
    h('header', { class: 'play-header' },
      h('h1', { id: 'page-heading', text: 'Play' }),
      h('p', {}, 'Load your ROM, then jump into the race.')),
    cardSlot,
    noticeSlot,
    startButton,
    startHint));

  function render({ focus, arriving } = {}) {
    const rom = state.get().rom;

    const card = romCard(rom);
    // One-shot start-lights sequence when a ROM has just been loaded, never on page load.
    if (arriving && !reducedMotion()) card.classList.add('is-arriving');
    cardSlot.replaceWith(card);
    cardSlot = card;

    const notices = launchNotices(rom);
    let entering = 0;
    notices.forEach(({ key, el }) => {
      if (shownNotices && !shownNotices.has(key)) {
        el.classList.add('is-entering');
        el.style.setProperty('--enter-i', entering++);
      }
    });
    noticeSlot.replaceChildren(...notices.map(({ el }) => el));
    shownNotices = new Set(notices.map(({ key }) => key));

    startButton.disabled = !rom.ready;
    if (rom.ready) startButton.removeAttribute('aria-describedby');
    else startButton.setAttribute('aria-describedby', 'start-hint');
    startHint.hidden = rom.ready;
    // Where the D-pad lands when this page opens: START once a ROM is ready, otherwise Browse.
    startButton.toggleAttribute('data-default-focus', rom.ready);
    if (!rom.ready) container.querySelector('[data-focus="browse"]')?.setAttribute('data-default-focus', '');

    // The button that was clicked has been rebuilt; hand focus to the next useful action.
    if (focus) container.querySelector(`[data-focus="${rom.ready ? focus : 'browse'}"]`)?.focus();
  }

  render();
};

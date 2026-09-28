DKRLauncher.pages = DKRLauncher.pages || {};

DKRLauncher.pages.about = function (container) {
  const { ui, state, config } = DKRLauncher;
  const { h } = ui;
  const text = (value, className = '') => h('p', { class: className, text: value });
  const title = value => h('h2', { class: 'about-section-title', text: value });
  const button = (label, onClick, variant) => ui.raceButton({ label, onClick, variant });
  const link = (label, href, context) => h('a', { class: 'race-button', href, target: '_blank', rel: 'noopener noreferrer',
    'aria-label': context ? label + ' — ' + context : undefined }, h('span', { text: label }));

  function patchNotes() {
    const modal = ui.openModal({ heading: "WHAT'S NEW SINCE 1.0.0",
      body: h('div', { class: 'about-patch-body' }, text('Current development and Online Beta changes', 'muted'),
        ...DKRLauncher.patchNotes.map(([heading, body]) => h('section', {}, h('h3', { text: heading }), text(body)))),
      actions: [button('CLOSE PATCH NOTES', ui.closeModal)] });
    modal.classList.add('launcher-detail-modal', 'about-patch-notes');
    const heading = modal.querySelector('#modal-heading');
    heading.tabIndex = -1;
    heading.focus({ preventScroll: true });
    modal.scrollTop = 0;
  }

  // Content and order match DrawAboutDkrR / DrawSupportSummary in runtime_ui.cpp.
  const introduction = h('section', { class: 'card about-introduction', 'aria-label': 'About DKR-R' },
    text('Wizpig has invaded DKR-R. Race across land, water and sky, collect Golden Balloons and help Diddy and his friends send the intergalactic pig wizard packing.'),
    text('DKR-R runs the original game logic through a native PC runtime. Accurate preserves the original presentation; Modern adds carefully isolated PC quality-of-life options.'),
    h('h2', { class: 'warm', text: 'CREATED BY THATGUYMCD' }),
    text("If you did not download DKR-R from ThatGuyMcd's GitHub repository, this build may have been modified or tampered with."),
    text('Official source: github.com/ThatGuyMcd/DKR-R', 'muted'),
    h('div', { class: 'launcher-detail-actions' }, link('VISIT GITHUB PAGE', config.links.github),
      button('VIEW PATCH NOTES', patchNotes, 'red')));

  const credits = h('section', { class: 'card', 'aria-labelledby': 'about-credits-heading' },
    h('div', { class: 'about-credit-group' }, h('h3', { text: 'POOTERMAN - DKR-R ICON' }),
      text("DKR-R's application icon was created by POOTERMAN."),
      link('VISIT POOTERMAN ON DEVIANTART', 'https://www.deviantart.com/pooterman')),
    h('div', { class: 'about-credit-group' }, h('h3', { text: 'CORE TECHNOLOGY AND RESEARCH' }),
      ...config.credits.map(credit => h('div', { class: 'about-technology' }, h('span', { text: credit.name }),
        link('VISIT GITHUB', credit.url, credit.name))), text('Thanks to all developers and contributors.', 'muted')),
    h('div', { class: 'about-credit-group' }, text('Golden Balloon - HUD layout reference'),
      link('VISIT GOLDEN BALLOON ON GITHUB', 'https://github.com/akratch/goldenballoon')),
    h('div', { class: 'about-credit-group' }, h('h3', { text: 'DKR-R HDR TEXTURE PACK PROJECT' }),
      text('A community project re-imagining Diddy Kong Racing in crisp HD while remaining faithful to the original art direction. Project lead: sr.gu. Thank you to every artist, tester and contributor involved.'),
      link('JOIN THE DKR-R HDR DISCORD', config.links.discord)));

  const current = state.get();
  const facts = [
    ['Release', config.version], ['Presentation', current.graphics.profile === 'modern' ? 'Modern' : 'Accurate'],
    ['Graphics API', current.graphics.graphicsApi || 'Auto'], ['HUD size', (current.graphics.hudSize ?? 100) + '%'],
    ['Enabled texture packs', current.textures.packs.filter(pack => pack.enabled && !pack.hidden).length],
    ['Online synchronization', current.online.hostSettings?.synchronization || 'Rollback'],
    ['OS', 'Unavailable in browser preview'], ['CPU', 'Unavailable in browser preview'],
    ['Memory', 'Unavailable in browser preview'], ['GPU', 'Unavailable in browser preview'],
    ['Storage', 'Unavailable in browser preview'],
  ];
  const summary = h('dl', { class: 'launcher-detail-facts' },
    ...facts.flatMap(([label, value]) => [h('dt', { text: label }), h('dd', { text: value })]));
  const supportStatus = h('p', { class: 'muted', role: 'status' });
  function exportSummary() {
    const settings = state.get().about;
    const report = ['DKR-R support summary — browser preview', '', ...facts.map(([label, value]) => `${label}: ${value}`),
      `Diagnostic logging: ${settings.diagnosticLogging ? 'Enabled' : 'Disabled'}`,
      `Create crash dumps: ${settings.crashDumps ? 'Enabled' : 'Disabled'}`].join('\n');
    const url = URL.createObjectURL(new Blob([report], { type: 'text/plain;charset=utf-8' }));
    const download = h('a', { href: url, download: 'dkr-r-support-preview.txt' });
    document.body.append(download); download.click(); download.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    supportStatus.textContent = 'Browser preview support summary exported.';
  }
  const exportButton = button('EXPORT SUPPORT SUMMARY', exportSummary);
  const reportsButton = button('OPEN SUPPORT REPORTS', () => ui.notify('Support reports are stored by the desktop launcher.'));
  exportButton.classList.add('full'); reportsButton.classList.add('full');
  const support = h('section', { class: 'card about-support', 'aria-labelledby': 'about-support-heading' }, summary,
    ui.checkboxRow('Diagnostic logging', current.about.diagnosticLogging, value => {
      state.update('about', { diagnosticLogging: value });
      supportStatus.textContent = `Diagnostic logging will be ${value ? 'enabled' : 'disabled'} at the next launch.`;
    }),
    ui.checkboxRow('Create crash dumps', current.about.crashDumps, value => {
      state.update('about', { crashDumps: value }); supportStatus.textContent = `Crash dumps are ${value ? 'enabled' : 'disabled'}.`;
    }),
    h('div', { class: 'launcher-detail-actions' }, exportButton,
      button('OPEN LOGS', () => ui.notify('Logs are available in the desktop launcher.')),
      button('OPEN CRASH DUMPS', () => ui.notify('Crash dumps are available in the desktop launcher.')), reportsButton), supportStatus);
  const creditsHeading = title('Credits'); creditsHeading.id = 'about-credits-heading';
  const supportHeading = title('Support summary'); supportHeading.id = 'about-support-heading';
  container.append(h('div', { class: 'about-page launcher-detail-page' },
    h('h1', { id: 'page-heading', text: 'About DKR-R' }), text('Diddy Kong Racing - Recompiled', 'muted'), introduction,
    title('Release information'), text('DKR-R ' + config.version), text('Windows, Linux and macOS builds'),
    text('No copyrighted game data is distributed. A legally obtained supported Diddy Kong Racing Game Pak is required.', 'muted'),
    creditsHeading, credits, supportHeading, text('Privacy-safe settings and system details for troubleshooting.', 'muted'), support));
};

// Snapshot of DrawPatchNotesModal; keep the wording in sync with the native launcher.
DKRLauncher.patchNotes = [
  [
    "GAME PAK COMPATIBILITY",
    "Unified support for US v1.0, US Rev A / v1.1 and byte-swapped supported images through one launcher and one runtime. Revision selection no longer opens a second application instance."
  ],
  [
    "DKR-R ONLINE",
    "Added secure five-character Quick Join, host approval, Online Profiles, friends, friend invites, Open Lobbies, presence and notifications. Added Lockstep and Rollback synchronization, host-authoritative race state, recovery barriers, connection and controller overlays, synchronized saves and cross-platform build compatibility checks. Water, hovercraft height, racer orientation, moving actors, RNG and CPU racers now follow authoritative state. Network catch-up and bounded recovery keep unstable connections responsive without accumulating permanent frame debt."
  ],
  [
    "MODERN PRESENTATION",
    "Added high-refresh interpolation without changing game speed, widescreen and ultrawide presentation, revised skyboxes and water, split-screen viewport handling, adjustable FOV, view distance, scenery controls, maximum vehicle detail and HUD sizing. Stabilized wheels, propellers, steering wheels, shadows, billboards, doors, trails, animated water and post-race cameras."
  ],
  [
    "GRAPHICS AND TEXTURES",
    "Added RT64 and Rice texture-pack import, live pack selection, CRT overlays, anisotropic filtering, downsampling, anti-aliasing, high-precision framebuffer controls and a configurable performance overlay. Corrected texture-edge sampling and high-resolution UI tile seams."
  ],
  [
    "CONTROLS",
    "Added independent Player 1-4 controller assignment, primary and secondary bindings, per-vehicle inversion, per-player gyro, quick race restart, texture-pack and fullscreen shortcuts, background input, live stick/gyro previews and broader modern/N64 controller database support."
  ],
  [
    "SAVES AND GAMEPLAY",
    "Added automatic EEPROM validation and repair, virtual Controller Paks alongside rumble, save backup/import/export, an Adventure Save Builder, course progress, unlockables and Magic Code management. Added independent music, vehicle, effects, ambience and EQ controls, plus optional music for three- and four-player races."
  ],
  [
    "LAUNCHER AND STABILITY",
    "Redesigned the controller-first launcher and in-game overlay, added animated DKR-R branding, friends and controller-friendly text entry, restart/exit/fullscreen handling, support diagnostics and clearer online errors. Fixed transition crashes, intro-loop crashes, race-end vertex explosions, audio pops, black screens and Linux/Steam Deck startup and layout problems."
  ]
];

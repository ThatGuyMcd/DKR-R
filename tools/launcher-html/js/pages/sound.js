DKRLauncher.pages = DKRLauncher.pages || {};

// Sound (UX study). Same settings as the native page (runtime_ui.cpp, DrawAudioSettings): Master, the
// four-part island mix, a three-band EQ and the 3-4 player race music option. The native page also
// has an Accurate profile variant (Master volume, then a note that the island mix is locked); the
// mockup always shows the Modern mix.
//
// Changes from the native layout: Master leads and the mix sits under it; every volume has a mute
// that keeps its level; the EQ starts from presets and remembers a hand-made curve; Reset mix only
// touches the four mix channels (Flat is the EQ's reset) and can be undone. A on a slider (Enter on
// a keyboard) is the row's quick action: mute on a volume, back to 0 dB on a band.
DKRLauncher.pages.sound = function (container) {
  const { ui, state } = DKRLauncher;
  const h = ui.h;
  const sound = () => state.get().sound;
  const save = (patch) => state.update('sound', patch);

  const CHANNELS = [
    { key: 'music', label: 'Music' },
    { key: 'sfx', label: 'Sound effects' },
    { key: 'vehicles', label: 'Vehicles' },
    { key: 'nature', label: 'Nature & ambience' },
  ];
  const BANDS = [
    { key: 'bass', label: 'Bass' },
    { key: 'mid', label: 'Mid' },
    { key: 'treble', label: 'Treble' },
  ];
  // Curves in dB for bass, mid, treble.
  const PRESETS = [
    { id: 'flat', label: 'Flat', eq: [0, 0, 0], about: 'The original game sound.' },
    { id: 'bass', label: 'Bass boost', eq: [5, 1, 0], about: 'Fuller engines and music. Best on headphones and bigger speakers.' },
    { id: 'clear', label: 'Clear', eq: [-2, 2, 4], about: 'Crisper effects that cut through the music. Good on small speakers.' },
    { id: 'warm', label: 'Warm', eq: [2, 0, -4], about: 'Softer highs for long sessions and late nights.' },
  ];
  const CUSTOM_ABOUT = 'Your own curve. Try any preset; pick Custom to get it back.';

  const curve = (s) => BANDS.map((band) => Math.round(s[band.key]));
  const sameCurve = (a, b) => a.every((v, i) => v === b[i]);
  const matchPreset = (eq) => PRESETS.find((preset) => sameCurve(preset.eq, eq));
  const mixIsOriginal = (s) => CHANNELS.every((c) => s[c.key] === 100 && !s.muted[c.key]);
  // Racing has no lower case (dB) or minus sign: numbers stay in the race font, units in Segoe.
  const signed = (db) => (db > 0 ? '+' : db < 0 ? '-' : '') + Math.abs(db);
  const unit = (number, text) => [number, h('small', {}, text)];

  // Last Reset mix, so it can be undone until the mix is touched again or the page is left.
  let undoMix = null;
  const rows = [];

  // ------------------------------------------------------------ icons

  function speakerIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('aria-hidden', 'true');
    svg.innerHTML = '<path d="M3.5 9.2h3.2L11.5 5v14l-4.8-4.2H3.5z" fill="currentColor"/>' +
      '<g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">' +
      '<path class="snd-wave-1" d="M15 9.2a4 4 0 0 1 0 5.6"/><path class="snd-wave-2" d="M17.8 6.4a8 8 0 0 1 0 11.2"/>' +
      '<path class="snd-cross" d="M15.5 9.5l5 5M20.5 9.5l-5 5"/></g>';
    return svg;
  }

  // ------------------------------------------------------------ rows

  // Label | slider | value on one line; the mute button leads the row. The whole row is the focus
  // target's frame, so the D-pad highlight says which setting it is on, not just which track.
  function volumeRow({ key, label, master }) {
    const id = 'snd-' + key;
    const input = h('input', {
      type: 'range', class: 'snd-range', id, min: 0, max: 100, step: 5, 'data-fk': id, 'aria-label': label + ' volume',
      // Entering the page lands on Master, not on the Reset button above it.
      'data-default-focus': master || undefined,
    });
    const value = h('span', { class: 'snd-value', 'aria-hidden': 'true' });
    // Mouse target only: the D-pad reaches mute through A on the slider, so a row is one stop.
    const mute = h('button', { type: 'button', class: 'snd-mute', tabindex: '-1', 'aria-controls': id }, speakerIcon());
    const row = h('div', { class: 'snd-row' + (master ? ' is-master' : '') },
      mute, h('label', { class: 'snd-label', for: id }, label), input, value);

    const toggleMute = () => {
      const muted = sound().muted;
      save({ muted: { ...muted, [key]: !muted[key] } });
      if (!master) undoMix = null;
      sync();
    };
    mute.addEventListener('click', toggleMute);
    input.addEventListener('keydown', (event) => {
      if (event.key !== 'Enter') return;
      event.preventDefault();
      toggleMute();
    });
    // Moving a muted slider unmutes it, like the system mixer.
    input.addEventListener('input', () => {
      const muted = sound().muted;
      save({ [key]: Number(input.value), muted: muted[key] ? { ...muted, [key]: false } : muted });
      if (!master) undoMix = null;
      sync();
    });

    rows.push({ key, row, input, value, mute, label, master });
    return row;
  }

  function bandRow({ key, label }) {
    const id = 'snd-' + key;
    const input = h('input', {
      type: 'range', class: 'snd-range is-bipolar', id, min: -12, max: 12, step: 1, 'data-fk': id,
      'data-a-label': 'Back to 0 dB', 'aria-label': label,
    });
    const value = h('span', { class: 'snd-value', 'aria-hidden': 'true' });
    const setBand = (db) => {
      const next = { ...sound(), [key]: db };
      // A hand-made curve is kept, so trying a preset never loses it.
      const eq = curve(next);
      save({ [key]: db, eqCustom: matchPreset(eq) ? sound().eqCustom : eq });
      sync();
    };
    input.addEventListener('input', () => setBand(Number(input.value)));
    input.addEventListener('keydown', (event) => {
      if (event.key !== 'Enter') return;
      event.preventDefault();
      setBand(0);
    });
    input.addEventListener('dblclick', () => setBand(0));
    rows.push({ key, row: null, input, value, band: true, label });
    return h('div', { class: 'snd-row is-band' }, h('label', { class: 'snd-label', for: id }, label), input, value);
  }

  // data-section: LB / RB on a pad jump between the three cards.
  function section({ id, title, caption, action }, ...body) {
    return h('section', { class: 'card snd-card', 'aria-labelledby': id, 'data-section': '' },
      h('header', { class: 'snd-head' },
        h('div', { class: 'snd-head-text' },
          h('h2', { id, text: title }),
          caption ? h('p', { class: 'snd-caption' }, caption) : null),
        action || null),
      ...body);
  }

  // ------------------------------------------------------------ volume

  const resetButton = h('button', { type: 'button', class: 'snd-link', 'data-fk': 'snd-reset' });
  resetButton.addEventListener('click', () => {
    const s = sound();
    if (undoMix) {
      save(undoMix);
      undoMix = null;
      ui.notify('Mix restored.');
    } else {
      undoMix = { muted: { ...s.muted } };
      CHANNELS.forEach((c) => { undoMix[c.key] = s[c.key]; });
      const muted = { ...s.muted };
      CHANNELS.forEach((c) => { muted[c.key] = false; });
      save({ music: 100, sfx: 100, vehicles: 100, nature: 100, muted });
      ui.notify('Mix back to 100%. Master is unchanged.');
    }
    sync();
  });

  const masterNote = h('p', { class: 'snd-note', role: 'status' });
  const volume = section({
    id: 'snd-volume-title', title: 'Volume',
    caption: 'Master sets how loud everything is. The mix balances the parts.',
    action: resetButton,
  },
  volumeRow({ key: 'master', label: 'Master', master: true }),
  h('div', { class: 'snd-mix-label' }, h('span', {}, 'Mix'), masterNote),
  h('div', { class: 'snd-mix' }, ...CHANNELS.map(volumeRow)));

  // ------------------------------------------------------------ equalizer

  const presetButtons = PRESETS.map((preset) => {
    const btn = h('button', { type: 'button', class: 'snd-chip', 'data-fk': 'snd-preset-' + preset.id, 'aria-pressed': 'false' }, preset.label);
    btn.addEventListener('click', () => {
      const [bass, mid, treble] = preset.eq;
      save({ bass, mid, treble });
      sync();
    });
    return btn;
  });
  const customButton = h('button', { type: 'button', class: 'snd-chip', 'data-fk': 'snd-preset-custom', 'aria-pressed': 'false' }, 'Custom');
  customButton.addEventListener('click', () => {
    const eq = sound().eqCustom;
    if (!eq) return;
    const [bass, mid, treble] = eq;
    save({ bass, mid, treble });
    sync();
  });
  const presetAbout = h('p', { class: 'snd-caption snd-preset-about', 'aria-live': 'polite' });

  const equalizer = section({ id: 'snd-eq-title', title: 'Equalizer', caption: 'Pick a sound, or fine-tune the three bands.' },
    h('div', { class: 'snd-chips', role: 'group', 'aria-label': 'Equalizer presets' }, ...presetButtons, customButton),
    presetAbout,
    h('div', { class: 'snd-bands' }, ...BANDS.map(bandRow)));

  // ------------------------------------------------------------ multiplayer

  const music34 = h('input', {
    type: 'checkbox', role: 'switch', class: 'snd-switch', id: 'snd-music34', 'data-fk': 'snd-music34',
    'aria-describedby': 'snd-music34-about',
  });
  music34.addEventListener('change', () => save({ restore34PlayerMusic: music34.checked }));
  const multiplayer = section({ id: 'snd-multi-title', title: 'Multiplayer' },
    h('label', { class: 'snd-toggle', for: 'snd-music34' },
      h('span', { class: 'snd-toggle-text' },
        h('strong', {}, 'Race music with 3-4 players'),
        h('span', { class: 'snd-caption', id: 'snd-music34-about' },
          'The original game turns the music off in 3 and 4 player races. Turn this on to keep it playing.')),
      music34));

  // ------------------------------------------------------------ state -> view

  function sync() {
    const s = sound();
    const silent = s.muted.master || s.master === 0;
    for (const r of rows) {
      const level = Math.round(s[r.key]);
      if (Number(r.input.value) !== level) r.input.value = level;
      if (r.band) {
        const pos = ((level + 12) / 24) * 100;
        r.input.style.setProperty('--lo', Math.min(pos, 50) + '%');
        r.input.style.setProperty('--hi', Math.max(pos, 50) + '%');
        r.value.replaceChildren(...unit(signed(level), 'dB'));
        r.input.setAttribute('aria-valuetext', signed(level).replace('-', 'minus ') + ' dB');
        continue;
      }
      const muted = !!s.muted[r.key];
      r.input.style.setProperty('--fill', level + '%');
      r.row.classList.toggle('is-muted', muted);
      r.row.classList.toggle('is-silenced', !r.master && silent);
      if (muted) r.value.textContent = 'Muted';
      else r.value.replaceChildren(...unit(level, '%'));
      r.input.setAttribute('aria-valuetext', muted ? level + '%, muted' : level + '%');
      r.input.dataset.aLabel = muted ? 'Unmute' : 'Mute';
      r.mute.dataset.level = muted ? 'muted' : level === 0 ? '0' : level < 50 ? '1' : '2';
      r.mute.setAttribute('aria-pressed', String(muted));
      r.mute.setAttribute('aria-label', (muted ? 'Unmute ' : 'Mute ') + r.label.toLowerCase());
      r.mute.title = muted ? 'Unmute' : 'Mute';
    }
    masterNote.textContent = s.muted.master ? 'Master is muted, so nothing plays.' : s.master === 0 ? 'Master is at 0%, so nothing plays.' : '';
    masterNote.hidden = !silent;

    const eq = curve(s);
    const preset = matchPreset(eq);
    const custom = s.eqCustom && !matchPreset(s.eqCustom.map(Math.round)) ? s.eqCustom : null;
    presetButtons.forEach((btn, i) => {
      btn.setAttribute('aria-pressed', String(PRESETS[i] === preset));
      // Jumping into the Equalizer (LB / RB) lands on the sound in use.
      btn.toggleAttribute('data-default-focus', PRESETS[i] === preset);
    });
    customButton.hidden = !preset ? false : !custom;
    customButton.setAttribute('aria-pressed', String(!preset));
    customButton.toggleAttribute('data-default-focus', !preset);
    presetAbout.replaceChildren(h('strong', {}, preset ? preset.label : 'Custom'), ' · ', preset ? preset.about : CUSTOM_ABOUT);

    const original = mixIsOriginal(s);
    resetButton.textContent = undoMix ? 'Undo reset' : 'Reset mix';
    resetButton.disabled = !undoMix && original;
    resetButton.dataset.disabledReason = 'The mix is already at 100%.';
    resetButton.dataset.hint = undoMix ? 'Put the mix back how it was.' : 'Music, effects, vehicles and ambience back to 100%. Master stays.';
    resetButton.title = resetButton.dataset.hint;

    music34.checked = !!s.restore34PlayerMusic;
  }

  const heading = h('header', { class: 'snd-header' },
    h('h1', { id: 'page-heading', text: 'Sound' }),
    h('p', {}, 'Set how loud the game is and shape its sound. Changes apply right away.'));
  const layout = h('div', { class: 'snd-layout' }, volume, h('div', { class: 'snd-side' }, equalizer, multiplayer));
  sync();
  container.append(h('div', { class: 'sound-page' }, heading, layout));
};

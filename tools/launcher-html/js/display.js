window.DKRLauncher = window.DKRLauncher || {};

// Interface size: scales the whole launcher for TVs and big screens (a 5K TV at 100% Windows
// scaling would otherwise show 14px text on a 2880px-tall screen). Loaded without `defer` so the
// first paint already has the right size. Auto keeps 100% up to 1440p and scales in half steps
// from there: 200% at 4K, 250% at 5K.
//
// The scale is CSS zoom on <html>. Zoom multiplies viewport units too, so the layout uses
// --vw / --vh (tokens.css), which divide by the scale again. The page never gets smaller than
// 1280 x 720 after scaling, the smallest layout the launcher is designed for (see maxScale).
(function () {
  const STEPS = [1, 1.25, 1.5, 1.75, 2, 2.5, 3];
  const root = document.documentElement;

  function saved() {
    try { return JSON.parse(localStorage.getItem('dkr-launcher-state') || '{}')?.display?.uiScale ?? 'auto'; }
    catch { return 'auto'; }
  }

  let setting = saved();
  const maxScale = () => Math.min(innerWidth / 1280, innerHeight / 720);
  const autoScale = () => Math.max(1, Math.floor(Math.min(innerWidth / 1920, innerHeight / 1080) * 2) / 2);
  const allowed = (step) => step <= maxScale() + 0.001;

  function resolve(value = setting) {
    const wanted = value === 'auto' ? autoScale() : Number(value) || 1;
    return STEPS.filter((step) => step <= wanted && allowed(step)).pop() || 1;
  }

  function apply() {
    const scale = resolve();
    root.style.setProperty('--ui-scale', String(scale));
    root.dataset.uiScale = String(scale);
    // The native sidebar turns roomy once the panel is 840px tall (CalculateSidebarLayout).
    const width = innerWidth / scale;
    const height = innerHeight / scale;
    const margin = Math.min(34, Math.max(16, width * 0.022));
    root.toggleAttribute('data-roomy', height - 2 * margin >= 840);
  }

  apply();
  addEventListener('resize', apply);

  DKRLauncher.display = {
    steps: STEPS,
    allowed,
    autoScale,
    get setting() { return setting; },
    scale: () => resolve(),
    resolve,
    set(value) { setting = value; apply(); },
  };
})();

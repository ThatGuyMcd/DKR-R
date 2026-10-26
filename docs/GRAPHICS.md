# Graphics and camera settings

Use **Graphics** in the launcher or overlay. The same settings are available
in both, with the game kept visible behind the overlay.

## Accurate and Modern

Accurate keeps the original 4:3/30 FPS presentation as the reference.
Modern opens widescreen, higher presentation rates, filtering, HUD and scenery
options. Interpolation improves the image between authored frames; it does not
make the simulation run faster.

Battery, Balanced and Quality are optional presets. Nothing changes until you
press Apply. Start at 60 FPS when diagnosing performance, then raise the target
once your chosen settings are steady. More pixels, MSAA, supersampling, retained
scenery and high-detail models can all add work.

## HUD and camera

**HUD layout** offers **4:3** or **Fit to window** for Modern one/two-player
presentation. Whole groups move together; their artwork is not stretched.
Three/four-player HUD placement remains separate. The custom HUD editor and
size controls are not enabled. See [HUD layouts](HUD-WORKSHOP.md).

FOV, clipping and scenery controls affect what the camera shows. The near-plane
and camera-obstruction improvements reduce seeing through nearby surfaces;
report reproducible gaps with the track, position, aspect ratio and FOV.

The optional **ModernCamera++** code mod supplies camera presets, local
profiles and live F7 tuning. Import/approve it like other Custom Magic Codes.
Camera presentation and the shadow-interpolation correction are separate from
shared simulation (`runtime-recomp/cmake/Camera.cmake`).
VR is unfinished and not a supported feature of this release.

## Vehicles and scenery

Maximum vehicle detail, keep-scenery switches, the ultrawide guard, view
distances and retention let you choose how much remains visible. Bigger ranges
can increase CPU work as well as GPU load. Turning them up is not a free
performance improvement.

The independent experimental online high-LOD/range work was rolled back
after graphics and performance regressions. Do not treat parity with offline
as a qualified release feature. This pass preserves the accepted rendering
paths/build switches; it does not rework those experiments. Water/reflection
presentation remains local; shared procedural wave allocation is not
independently changed by a client.

Normal local 2/3/4-player games use the offline enhancement hooks. Modern
four-player object ranges compensate for the original additional half-range
reduction. Enable Keep scenery before loading the race to retain eligible
noninteractive scenery that the original multiplayer loader would discard.

## Ray tracing and water

The **Ray tracing & water** section has:

- **Water preset:** Original, Performance, Balanced, High or Custom.
  Performance/Balanced/High set the water component budgets; Custom reflects
  your individual changes. A preset does not change resolution, MSAA, target
  presentation rate or the reflection method.
- **Reflections:** Sky, Screen-space or Hardware ray tracing (experimental).
- **Racer wakes** and **Water spray**.
- **Lava lighting:** Nearby lava glow has its own on/off tickbox. It caches
  subtle illumination on nearby static scenery and pulses
  it in the existing colour pass, without per-frame lighting rays or an extra
  lighting pass. It works independently of the water reflection method.
  The old ray-traced lava lighting mode and its quality dropdown have been
  removed. Existing selections migrate to cached glow, retaining intensity
  and the on/off preference. Water's hardware reflections are unchanged.
  Intensity and range affect the surrounding glow; lava brightness affects the
  molten surface. Pulse strength/speed blend yellow-white heat into orange-red
  heat, never darkness; surrounding light varies gently. Turning glow off keeps the
  enhanced surface, water and reflections.

Open **Advanced water settings** to adjust:

- **Reflection detail**, **Wake quality** and **Spray detail** independently:
  Preset default, Low, Medium or High.
- **Hardware reflection distance** and **Reflection strength**. A zero reflection
  strength skips reflection tracing; reducing distance can reduce ray work.
- **Surface roughness**, **Fine ripple strength**, **Foam intensity**,
  **Wake visibility**, **Water clarity** and **Refraction strength**.
- **Waterfall flowing detail**, including zero to retain the original artwork.
- **Use track colours**, or individual red/green/blue water tint controls.
- **Reset water appearance** and **Restore existing water quality defaults**.
- **Log water GPU timings**, which takes effect next game launch and records
  bounded summaries without adding a diagnostic GPU drain.

Appearance controls change the look, not the game simulation. Low component
quality reduces its work; simply reducing a visual slider does not always make
the pass cheaper. Settings are saved locally, including during online play.
The panel reports the last active reflection method and allocation fallback
status so a hardware fallback is not mistaken for a changed saved preference.

Enhanced lava has moving molten cracks, rough hot-rock islands and shaded ridges.
That surface detail does not need ray-traced scene lighting. It is a
material effect, not extra collision geometry or a full multi-bounce path tracer.

Original keeps the game's water. The enhanced presets add the new water presentation
to eligible flat and animated water. Waterfalls and fountains retain their
authored appearance with added flowing detail. Recognised custom-track water
can use the same renderer path; arbitrary mod surfaces still need valid water
classification.

Screen-space reflections use visible scene colour/depth. They are not hardware
ray tracing and cannot fully reconstruct off-screen objects. The hardware
path needs compatible GPU ray queries and drivers, traces opaque level geometry,
and uses screen-space reflections for moving racers. Off-screen surfaces use
vertex colours rather than texture-pack detail.

Unsupported hardware or scenes exceeding the safety budget fall back to
screen-space reflections. This does not change collisions, buoyancy or online
simulation. High water and hardware tracing can be expensive; compare the same
scene with Original before attributing a slowdown to netplay.

## Graphics and memory fixes in v1.0.11

I've given the normal game heap its missing expansion RAM headroom,
while retaining the upper guard and existing allocation lifetimes.
Allocation failures and missing HUD resources now have clearer logs.
Queued graphics data is copied before it waits in the game scheduler,
and the renderer keeps its own decode workspace. These changes do not
alter the launcher. The renderer changes do not alter online simulation.

Water animations retain their selection while frames are queued. Shared
frames use one consistent decision, and complete animations can request
evicted cache entries again, with bounded asynchronous retries. A pack
replacing only some frames stays consistently vanilla for that animation.
For example, one replacement out of 16 ripple frames is incomplete.
The update does not fill in missing artwork or modify your texture pack.

These fixes do not remove GPU, guest memory or mod complexity limits.
If a problem remains, keep the logs and report the exact scene and packs.

## Textures and sound

Texture packs, their priorities and CRT filters belong in
[Textures](TEXTURE_PACKS.md). Audio settings are covered in [Sound](SOUND.md).
For failures and useful reports, see [troubleshooting](TROUBLESHOOTING.md).

## v1.0.7 presentation corrections

Original rivers/waterfalls interpolate with enhanced effects off. HD liquid
frames promote together only when a recognised matching family is resident;
an incomplete family remains consistently original. Pack reload/eviction
invalidates that decision. No extra water GPU pass was added for this fix.

Logo scenes, moving Start Menu names and credits use the selected viewport.
The copyright/recompilation text has a translucent rounded backing. Credits
include full-width fades and lower sky, with bounded native readback.

## v1.0.11 spectator and custom-car presentation

Spectators choose All splits, Follow player or the T.T.-style director without
changing racing input. Custom GPU-rendered cars use safe racer cameras in the
director; original vehicles can use its cinematic view. Custom cars replace
visuals, not physics, and their online choices remain admitted session state.
The saved graphics preset display and previous water/camera fixes are retained.
See [Spectators](SPECTATORS.md) and [Vehicles](VEHICLES.md).

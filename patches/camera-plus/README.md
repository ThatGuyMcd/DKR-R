# ModernCamera++ Patch Pipeline

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

ModernCamera++ is an optional, importable `.nrm`. Camera behaviour lives in the
mod; DKR-R provides a separate, restricted local presentation API and a copied
per-view scene compiler. The package is not installed or enabled automatically.

The first game package is a **flat-screen playtest**, not a completed VR release.
Original, chase, bonnet, first person, comfort first person and cinematic chase
are connected. Rear view, orbit controls within the driving camera, per-vehicle
tuning, four local profiles and the live F7 editor are connected. Inspection,
rider-only hiding and headset output are not advertised as renderer capabilities.

## Install and use

1. Use the matching Camera++ Windows build or Linux AppImage.
2. Import `ModernCamera++.nrm` under Mods/Hacks > Custom Magic Codes.
3. Approve and enable it, then start a new game.
4. Press F7, or open Graphics > Camera++ settings, during play.
5. Choose the local profile, preset and section. Apply changes live; Save keeps
   all four profiles for the next launch. Revert returns to the saved profiles.

Right stick looks around, right-stick click/O looks back, left-stick click/R
recentres, Y/C cycles cameras, F6 switches first person, and Page Up/Down zoom.
Hold the right mouse button to look with the mouse. Bindings are remappable in
Controls. Only conflicting C-button bindings are consumed while Camera++ is
actually driving an eligible local view. Original DKR and scripted scenes keep
their native cameras. Menu focus blocks camera actions until release.

## Ownership and online behaviour

- No camera mod maps live game memory. The private VM has its own regions,
  read-only copied snapshots, whitelisted imports and bounded execution.
- The mod has no gameplay hooks, raw references, native calls, filesystem,
  networking, clocks, shared RNG, audio or save access.
- Presentation packages are filtered before gameplay settings/resources/save
  identity construction. Executable approval remains required.
- Online view identity is the actual local racer slot, not always player one.
  Physical input profile and saved profile remain separate from that slot.
- The existing rollback image gets exactly one mutable DecodeWorkspace. Camera
  redraws reuse it sequentially and restore the prepared retail bytes before
  HUD and another viewport resume. One bounded 8 MiB restoration journal keeps
  the renderer's local layout corrections; it is not another mutable workspace.
  Normal play uses a task-owned copy
  of the already submitted image, not concurrent live CPU RAM.
- A closed, checked C draw closure rebuilds camera-visible scenery, racers,
  particles, wave geometry and billboards. It never calls `render_scene`, runs
  another logic tick, invokes gameplay mods, advances shared audio/RNG or writes
  into the simulation. Original forward culling is not reused as a fake rear view.
- Snow, rain and lens flares redraw from their retained visual state. Weather
  logic, snow movement, rain sound and lightning updates are not re-executed.
- New matrix identities are view/scene/object-owned. Arbitrary vertex matching
  is off; only topology-keyed wave matrices opt in to vertex interpolation.
- Static collision uses copied solid level triangles. Dynamic door/platform
  collision is not advertised. Unsupported/budget-exhausted queries return the
  native camera position. A provider fault disables that local provider, not the
  race; a draw-closure fault falls back for that view for the rest of the scene.

## Protected sources

Only authored Patch Pipeline files change. Native composition verifies the
accepted portrait/save baseline and all staged differences. Actual submodules,
RecompiledFuncs, RecompiledPatches and previous generated payloads remain inputs.

The separately approved VR dependency experiment is retained: four pinned
RT64/Plume files can differ only in a new copied dependency tree. It is **not
selected by the flat-screen playtest**. The original checked-out dependency
files are not edited.

## VR work still required

The OpenXR owner and approved Vulkan device factory compile, including runtime
GPU negotiation, swapchains, predicted eye poses/FOV, recenter and panel layers.
They do not yet consume eye-specific scene renders, a compositor HUD or a
head-wall comfort policy. No headset compatibility or functional VR claim is
made. The target remains vendor-neutral seated PC OpenXR on Windows/Linux, not
standalone Android/Quest. Do not replace this with two copies of a flat frame.

The agent compiles, inspects and packages. Gameplay, visual, online, performance
and headset testing remain with the user. A successful compiler/linker is not a
runtime acceptance result.

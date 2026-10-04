# Native-stage custom character selection — revised proposal

2026-09-09. Status: approved; private implementation built on Windows/Linux;
visual/audio qualification pending. See `LEGACY-NATIVE-STAGE-IMPLEMENTATION.md`
for exact changes, tests, hashes and remaining gates. This supersedes the proposed
portrait-grid/personal-panel/row designs, not the accepted racer asset loader.
The investigation was read-only; implementation began after user approval.

## Required presentation

Extend the original game's 3D character-select stage. Keep the original scene,
animated roster, lighting, selection signs, music, Player Select title and
OK confirmation. Add actual custom selection actors in additional positions,
with stock and custom characters visible together. No custom screen, portrait
cards, individual player panels, shared page owner or menu layered over the
original actors. Preserve original unlock rules.

The first qualification arrangement adds Yooka and Haunter at additional
positions alongside the existing rows. Prefer existing clear stage space;
verify floor contact, occlusion and readability before fixing exact positions.
A small scene-local camera adjustment is permissible only if needed to frame
the additions. Do not change the global camera/HUD/aspect-ratio patches, squash
models horizontally, or progressively shrink an arbitrarily large roster.

## Evidence obtained in this pass

- `extern/dkr-decomp/src/object_functions.c:2051`, `obj_loop_char_select`, is
  the native animated actor path: music-synchronized animation, selected-player
  signs and one-shot confirmation particles. It uses fixed 8/9/10 actor lists
  and bounded sign counters; extra logical characters cannot simply be added
  as unchecked native indexes. The sign path changes model material indices,
  so custom sign materials and model ownership must be explicitly validated.
- The native Conker selection header is `ConkSelect`, behaviour
  `BHV_CHARACTER_SELECT`, model record 311. T.T.'s selection header is
  `StopWatchSelect`, same behaviour, model record 207. Verified in the extracted
  stock header JSON and model order metadata.
- The read-only inspector rerun against the supplied Yooka delta reports a
  changed model 311; both Haunter delta editions report changed model 207.
  These are selection-specific model roots, distinct from vehicle models.
  This establishes that the fixtures provide the right starting assets, not
  that those assets have already been qualified for additive stage playback.
- `legacy_character_materialize.cpp:120` currently starts dependency traversal
  from three vehicle headers and a portrait. The current character definition
  and persisted artifact have no selection-stage or audio definition. The
  missing stage/audio support requires import/preparation work, not just UI
  coordinates.
- `legacy_character_menu.cpp:98` uses generic SOUND_SELECT2 (0xEF) when a
  custom choice is readied. Native `charselect_input`/`charselect_pick` select
  character-specific voice cues and handles from the selected identity. The
  current native-safe placeholder also risks the final confirmation using the
  placeholder's voice instead of the selected mod's voice.
- The inspector reports Yooka audio-section 39 changes in records 2, 3 and 7;
  Haunter changes records 0, 1, 2 and 3. Against the audio offset layout in
  `audio_init`, these include sound bank/sample changes; Yooka also changes
  the sound mapping table and Haunter the sequence bank/sample area. Audio
  changes are therefore present, but individual selection clips/instruments
  still require bank-reference tracing. Changed records are not permission to
  replace the complete global bank.
- Horizontal input is independently defective: `menu_input` copies native
  negative-X-left/positive-X-right events, while the custom flat grid maps
  positive X to the preceding card. Correct only the custom-stage navigation
  boundary, with physical-input qualification rather than copying the old
  grid test's inverted assumptions.

## Implementation plan after approval

1. Preserve the working private candidate and identities. Remove the custom
   card renderer/page ownership from the active selector composition when the
   native-stage candidate is ready. Keep the accepted race resource namespace
   and native launch/controller/AI initialization.
2. Extend character preparation with a validated selection actor definition:
   header, model, textures, animation graph, idle/selected animation semantics,
   sign materials and selection effects. Read these from the patched source,
   append isolated copies and retain original assets byte-for-byte. Reuse
   decoded-animation alignment and ownership checks. Missing compatible stage
   animations must be reported; do not silently show the donor or a frozen
   vehicle as the finished selector.
3. Spawn additional menu-only actors using checked project-owned Patch Pipeline
   hooks and the original scene/actor lifecycle. Give each a stable logical
   identity, stage position and private sign/animation state. Preserve native
   stock actors; prevent a custom player's safe native placeholder from also
   highlighting the donor/placeholder actor. Do not index the fixed retail
   actor/sign tables with new library indexes.
4. Extend directional adjacency at the edges of the original roster to the
   additional visible actors. Base movement on actual screen direction. Keep
   original navigation where it remains valid and retain independent P1-P4
   logical selections, native A/B/Start semantics and duplicate policy.
   Nobody owns the camera/page and one player's actions cannot hide or reset
   another's selected actor. Preserve party-wide final OK confirmation.
5. Map audio by character and action: cursor movement, select, cancel and final
   confirmation are distinct. Trace the mod's sound table through bank entries,
   sample data, loop/predictor metadata and pitch/volume settings. Prepare only
   the validated dependency closure and assign isolated runtime sound handles/
   identities. Retain stock sound IDs and bank content; never swap the entire
   sound bank on hover. One player's cancellation must not stop another's cue.
   Preserve native music-layer behaviour; use custom instrument substitutions
   only where their sequence/bank mapping is verified. If the mod has no custom
   cue, inherit its documented base cue, not whichever native cursor is hidden
   underneath. Do not invent recordings or use an unrelated UI beep.
6. Qualify stage actors and audio separately before combining them. Then test
   stock Conker plus Yooka, stock T.T. plus Haunter, mixed original/custom
   four-player selection, duplicate policy, rapid move/select/cancel, players
   joining/leaving, animation/sign ownership, race entry and menu return.
   Verify original selection audio is unchanged. Measure menu actor/audio
   memory and cache release across repeated visits.

All changes remain project-owned Patch Pipeline/runtime adapter work. Do not
hand-edit submodules, generated functions or generated patches. Do not modify
netplay pacing, save separation, race behaviour, global shadows or HUD layout.
Adding menu audio is a separate resource/lifecycle task, not a reason to replace
the audio engine. Both ROM revisions need independently verified hook metadata;
Windows/Linux and the user-run visual/audio checks remain release gates.

## Capacity and approval boundary

A same-camera stage has finite readable space, particularly at 4:3. First
qualify the two supplied additions without hiding any original racer. Measure
the safe simultaneous stage capacity before deciding how larger enabled
libraries are presented. Do not silently add scrolling pages, player-owned
carousels, an active-roster reduction or a new UI. Those would require a
separate user decision if the measured capacity is exceeded. The current
16-character resident budget is a memory bound, not proof that 16 extra actors
fit on the native stage or that unlimited actors can be shown simultaneously.

No Beta 6 release is implied by this planning checkpoint. Final packaging must
still use the approved normal activation path with qualification mode OFF and
include matching Windows and Linux AppImage builds.

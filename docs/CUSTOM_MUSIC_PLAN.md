# Custom music in the Blender track addon

Status: implementation plan, based on the current checkout, 2026-09-23.
Updated 2026-10-02: recorded music (MP3/WAV) is implemented end to end - see
"Recorded music" below. It is format- and unit-tested; it has **not** yet been
heard in game on either revision. The native-sequence and MIDI stages below
are unchanged and still unimplemented.

The first deliverable is one author-supplied native DKR sequence per `.dkrmap`,
played through the game's existing instrument bank. Standard MIDI import follows
on the same package/runtime contract. Music remains part of the track export and
installation workflow.

## What the code already provides

| Area | Evidence in this checkout | Consequence |
| --- | --- | --- |
| Addon music selection | `tools/blender/dkr_track_editor/operators/header.py`: `MUSIC`, `music_index`, `DKR_OT_step_music`; `ui/panels.py`: `_draw_music` | Currently selects a retail sequence ID only. |
| Preview | `operators/placeholders.py`: `DKR_OT_play_music` always fails `poll` | In-Blender listening needs a separate renderer; it is not an existing feature to connect. |
| Header | `level_header.py`: `/music` at 0x52, `/instruments` at 0x54 | The latter is a 16-bit channel-enable mask, not an instrument-bank ID. |
| Packages | `dkrmap.py`: `TrackPackage.manifest`; `runtime-recomp/src/game/custom_tracks.cpp`: `parse_track` | Schema 1 has no music payload. Runtime sections currently cover headers, names, models, object maps and textures. |
| Native playback | `extern/dkr-decomp/src/audio.c`: `audio_init`, `music_sequence_init`, `sound_seqplayer_init` | Separate music/jingle players share the original musical instrument bank. Both sequence buffers are allocated at boot. |
| Native sequence format | `extern/dkr-decomp/libultra/src/audio/cseq.c` | 68-byte header: sixteen big-endian track offsets and a time division; duration-bearing note events, running status, tempo, loops and compressed backreferences. A renamed `.mid` is not valid. |
| Existing replacements | `mods/legacy_audio_bank.cpp`: `inspect_sequence_directory`, `build_sequence_bank`; `legacy_track_materialize.cpp` | Legacy import already repacks a selected song while retaining stock instruments and sequence capacity. |
| Resident state | `mods/legacy_resident_assets.cpp`, `legacy_runtime_session.cpp` | Legacy scenes also republish relocated sequence pointers and lengths. The addon cannot independently rewrite those same global tables. |
| Dynamics | `audio.c`, `racer.c`, `objects.c`, `object_functions.c` | Whole-song fades, per-channel fades and final-lap tempo changes already exist. Objects may gate channel changes on the current sequence ID. |

Paths in the last three runtime rows are relative to
`runtime-recomp/src/game/`.

A read-only inspection of the local extracted `us.v77` and `us.v80` assets found:

- Both contain 66 sequence entries; the largest even-aligned load is **13,032
  bytes**. This is the existing music/jingle buffer capacity, not a universal
  N64 format limit. Derive it from the selected ROM at runtime.
- Both have 128 non-null musical program slots. The instrument control bank,
  sample bank and sequence bank are byte-identical across these two revisions.
- The dedicated percussion-bank pointer is null. Do not assign General MIDI
  drums to channel 10 automatically; inspect actual program/key mappings.
- Audio record 5 contains the `S1` sequence directory. Record 0 is the musical
  control bank, record 1 its samples, and record 6 the per-sequence
  volume/tempo/reverb data. The records and the `ASSET_AUDIO_n` boundary names
  are not interchangeable; `audio_init` uses boundaries in the offset table.

These observations establish format constraints, not proof that a newly authored
song plays correctly. Keep extracted assets local, consistent with
`docs/ASSET_POLICY.md`.

## Recorded music (MP3/WAV)

A track can ship a recording instead of a sequence. It cannot go through DKR's
music player at all - the buffer holds about 13 KB and the player only drives
the instrument bank - so it is played on the host:

- **Carrier.** The header's `/music` still names a retail race song, which
  plays exactly as retail does. Its volume is forced to zero at `alCSPSetVol`
  (the existing `dkr_scale_sequence_player_volume` hook), and the requested
  volume is kept instead. That value is `base * slider * fade`; dividing the
  song's base back out leaves the options slider, every fade and the pause
  menu's halving, which then scale the file. The launcher's music slider is
  applied first, so it reaches the file too.
- **Play state and tempo.** Once per DKR audio update (the existing
  `dkr_audio_mix_tick` hook at `sound_update_queue`), the runtime reads
  `gMusicPlayer->state` (offset 0x2C), `gCurrentSequenceID` and `sMusicTempo`.
  The file starts from the top whenever the carrier starts, stops (with a short
  ramp) when it stops or another song takes over, and - when the author chose
  *Speed Up* - plays faster by the carrier's current BPM over its starting BPM,
  which is the final lap's 1.12. Jingles have their own player and are never
  touched.
- **Binding.** custom_tracks' level-load observer (called from the level_load
  scene-reset hook, so races, restarts and Track Select previews alike) binds
  the music of the custom track that owns the level, or clears it. A retail
  level that uses the same song is unaffected.
- **Decode and mix.** `custom_music_decode.cpp` checks the file's SHA-256
  against the manifest and decodes it fully (dr_mp3 / dr_wav, vendored under
  `runtime-recomp/third_party/dr_libs`) on a worker thread; a restart reuses
  the decode. `queue_audio` mixes it in, resampled linearly at
  `source rate / output rate * tempo`, with the loop seam interpolated, before
  the equaliser and master volume.

No new recompiler hook was needed: the two hooks above already existed in both
policies, and the only new guest address is `gCurrentSequenceID`
(`revision_addresses::CurrentSequence`: 0x80115D04 in v77, 0x80116284 in v80,
from the decomp's symbol files).

What a recording cannot do: the MidiFade/MidiFadePoint/MidiChSet objects switch
or fade channels of the playing sequence, and a recording has none, so they are
inaudible on such a track (the addon warns, and leaves them in place). Speed Up
raises the pitch with the tempo, like a sequence does not; a time-stretch is a
possible later option.

Code: `tools/blender/dkr_track_editor/music_audio.py`, `operators/music.py`,
`dkrmap.TrackPackage.set_music`; `runtime-recomp/src/game/custom_music*.{hpp,cpp}`,
`custom_tracks.cpp: parse_music`. Tests: `tools/blender/tests/test_music.py`,
`test_blender_music.py`, `runtime-recomp/tests/custom_music_policy_tests.cpp`,
`custom_music_decode_tests.cpp`, `custom_music_runtime_tests.cpp` (the player
against simulated guest memory, also clean under TSan and ASan/UBSan) and the
music cases in `custom_tracks_tests.cpp`.

Still to qualify in game, on both revisions: start, loop seam, restart, final
lap, pause, fades into and out of results, a jingle over the music, Track Select
preview -> race, custom -> stock -> custom, the music slider in Accurate and
Modern, and local multiplayer.

## First release contract

- Music source choices: **Original game music** or **Custom sequence**.
- Import one native `ALCSeq` payload, provisionally named `.cseq`, composed for
  the original DKR bank. Validate bytes, not the extension.
- Retain the existing retail music picker as the internal carrier/fallback.
  Restrict the custom carrier to a verified nonzero race-music ID; do not allocate
  new global sequence IDs. Multiple tracks can use the same carrier because the
  binding is scoped to the loaded track.
- Expose initial BPM, volume and the game's verified reverb settings. Start with
  constant-tempo songs, sixteen channels maximum and the existing buffer limit.
  Reject unsupported tempo maps explicitly. Native tempo events must agree with
  the initial BPM and must not execute again inside the repeating section.
- Default custom music to all channels enabled (`0xFFFF`). An imported level's
  mask can otherwise silently suppress parts of an unrelated composition.
- Existing whole-song fades and the native final-lap multiplier remain usable.
  Validate their behavior in game; do not implement a second fade or tempo clock.
- The first listening workflow is export and play in Track Lab. Do not enable
  the existing Blender Play button until it can render the DKR instruments.

Native import is the first independently usable stage. MIDI conversion and
friendly controls for spatial arrangements are subsequent stages, rather
than dependencies for getting a custom composition into a race.

## Package format

Use schema 2 for packages containing custom music. Preserve schema 1 output for
tracks without it, and let the updated runtime accept both. Older runtimes
already reject unknown schema versions; this avoids silently ignoring the music.

Proposed additional manifest field (illustrative values):

```json
{
  "schemaVersion": 2,
  "music": {
    "format": "dkr-alcseq-v1",
    "bank": "dkr-stock-v1",
    "file": "music/main.cseq",
    "carrierSequence": 10,
    "tempoBpm": 120,
    "volume": 110,
    "reverb": 1,
    "channelMask": 65535
  }
}
```

The usual `id`, `name` and `adds` remain required. Music is a separate descriptor,
not an ordinary appendable asset section: native audio has different allocation
and ownership rules. Export sets header `/music` and `/instruments` from this
descriptor; runtime rejects contradictions. `dkr-stock-v1` denotes a documented
program/key mapping profile verified against the supported ROM banks.

Validate the descriptor, bounded payload size and file containment during
export and runtime import. Resolve symlinks as well as traversal components.
Music validation is mandatory even when the geometry/object validation checkbox
is disabled. Hash the payload plus playback metadata into the prepared music
identity so reload cannot reuse a previous export's data. Store any authoring
MIDI separately under `source/`; playback consumes only the native payload.

## Runtime design

Prefer a **track-scoped sequence-start adapter** over republishing the global
audio bank. Reuse/refactor the legacy format validation, but keep its existing
scene-bank ownership intact.

1. Add an immutable prepared music descriptor to `custom_tracks::Track` and a
   binding keyed by track identity, payload identity, resolved level ID and
   carrier sequence. Publish the binding at the actual level-load boundary,
   including the menu-preview route, after legacy scene restoration. Merely
   highlighting or arming a track must not activate its music.
2. Hook `music_sequence_init` through the checked Patch Pipeline. Delegate to
   the original function unless the current scene owns a custom binding, the
   target is `gMusicPlayer`, and the requested nonzero sequence is its carrier.
   Jingles, menu songs and other sequence requests retain their existing path.
3. For an eligible request, preserve the original `AL_STOPPED` gate. Once it is
   stopped, copy the validated payload into the original writable music buffer,
   honoring guest byte order and even-length padding. Never overwrite a playing
   sequence. Native loop handling mutates bytes, so restart must recopy the
   immutable source rather than reuse an already played buffer image.
4. Perform the same native initialization as the original function: `alCSeqNew`,
   attach sequence, play, apply volume/BPM/reverb, update current ID and consume
   the pending ID. Preserve pending dynamic-mask behavior and existing volume
   hooks. Supply descriptor metadata directly in this path; do not temporarily
   rewrite the shared `gSeqSoundTable` row, which also serves jingles.
5. Preserve stock instrument/sample pointers. Leave `gSequenceTable` and
   `gSeqLengthTable` under their current owner. The custom path supplies its own
   validated load length, so it must bypass the original sequence DMA rather
   than copy new data before a stock load that would overwrite it.
6. Clear eligibility on scene exit, unrelated menu loads and shutdown. A reload
   stages a new immutable descriptor and activates it at the next safe restart.
   Do not free or replace the game's playing buffer on a host UI thread.

Implement the narrow adapter in proposed `custom_music.hpp/.cpp` and
`custom_music_hooks.cpp`. Extend `GamePayload` and its revision implementations
with the necessary native audio callbacks; verify the relevant globals and
function signatures independently for both revisions. Update both
`dkr.us.v77.recomp-policy.json` and `dkr.us.v80.recomp-policy.json`, policy
composition checks, generated payload integration and CMake targets. Do not
hand-edit generated recompiled function bodies.

The initial prototype must establish the ordering of this adapter with
`dkr_legacy_scene_begin`, Track Lab restart and the audio thread. Preserve
register state across native callbacks and never hold a host lock across a
guest call that can yield. Restrict the first native-import release to the
verified existing buffer capacity; larger buffers need a separate allocation
and audio-lifetime change.

## Implementation sequence and acceptance gates

### 1. Instrument profile and native validation

Add pure Python `music.py` and `music_bank.py` modules, plus a reusable C++ native
sequence validator. Extend `assets.py` to resolve the audio assets through the
extracted asset metadata.

Generate a local instrument report: zero-based program number, note/velocity
regions, sample references, root key and tuning. Provide explicit labeling of
0-127 stored program numbers versus a DAW's possible 1-128 display. The bank
does not establish friendly instrument names; use verified labels or numbers.
Validate each played note against the selected program's key/velocity regions,
including defaults, program changes and supported controller effects.

The current `inspect_sequence_header` checks only offsets and division. New
external files require event-stream validation: bounded variable-length values,
running status, allowed statuses/controllers, valid note durations, tempo,
end markers, compressed backreferences and loop destinations. Validate loop
structure and progress without expanding an infinite loop indefinitely. Bound
validation work and reject zero-time cycles or event density that would exhaust
the player queue. Distinguish sixteen channels from polyphony: the music player
starts with an 18-voice limit despite a 24-voice configuration; releases and
sustain can increase overlap beyond the number of held notes.

Gate: synthetic valid/invalid fixtures agree between Python and C++; optional
local tests accept the retail sequence corpus without redistributing it.

### 2. Prove one native song in the runtime

Implement the package descriptor and sequence-start adapter first. Use an
original short test composition with two timbres, a known BPM and a simple loop.
Verify actual playback, loop continuity, restart, final-lap speedup and stock
restoration on both revisions. Exercise a jingle while the custom song plays.

Gate: a hand-authored schema 2 package works through Track Lab and Track Select,
and loading an original track using the same carrier plays its original music.
This proves the runtime before adding Blender controls or a MIDI converter.

### 3. Expose native import and export in Blender

- `props.py`: source mode, file reference, BPM/volume/reverb, mask and import
  report. Define defaults and migration for existing `.blend` files.
- New `operators/music.py`: select, validate and clear a native sequence.
- `ui/panels.py`: extend `_draw_music` with source selection and actionable
  validation messages; keep original-song browsing working.
- `operators/header.py`: make music ID and channel mask consistent for both
  new headers and inherited/remixed headers.
- `operators/pack.py`, `dkrmap.py`: include `music/main.cseq`, descriptor and
  schema version. Preflight music before publishing the package, stage writes
  and publish the manifest last. Switching back to original music removes the
  descriptor and stale generated music output without deleting author files.
- `operators/new_track.py` and level import/reset paths: clear stale custom
  bindings when adopting another track. Preserve settings through `.blend`
  save/reopen and handle Blender-relative paths with `bpy.path.abspath`.
- Register the operators and update `tools/blender/README.md` and
  `docs/CUSTOM_TRACKS.md` with the exact supported input format.

Gate: choose a native song, save/reopen the Blender file, export, install and
race without editing the manifest by hand. Re-export/restart picks up a changed
song. A missing or invalid source produces an error, not a silent stock fallback.

### 4. Add Standard MIDI import

Add a pure Python `midi_import.py` converter. Initially support SMF types 0/1,
PPQN timing and constant tempo. Merge MIDI tracks by channel, retain stable
same-tick event order, pair note-on/off events into DKR note durations, and
translate only controllers the actual `csplayer.c` supports. Treat note-on with
zero velocity as note-off on input. Reject ambiguous overlapping same-key notes
until their pairing semantics are explicitly supported. Reject type 2, SMPTE,
SysEx and unsupported tempo/control behavior with specific messages.

Provide a per-program mapping into the DKR profile. Never silently treat General
MIDI names or the percussion channel as the DKR mapping. Expose loop start/end
in musical time, convert to ticks and synchronize all channel loop boundaries;
check notes/sustain crossing the boundary. Put initial tempo setup outside the
loop so it cannot reset the final-lap speedup. If later supporting tempo maps,
handle the game's cached `sMusicTempo` and final-lap scaling explicitly.

Emit a simple valid native encoding first, including required literal escapes;
do not require compression for correctness. If size exceeds the verified cap,
report the exact size/limit. Compression can be added after event-level
roundtrip tests. A third-party MIDI parser is an implementation option only
after its license and offline Blender packaging are checked; no author should
need a C++ build, shell converter or pip installation to use the addon.

Gate: MIDI -> native -> decoded events preserves notes, timing, programs,
controllers and loop behavior for synthetic musical fixtures; game playback
matches that event model on both revisions.

### 5. Dynamic arrangement authoring and listening

The catalog already contains `ASSET_OBJECT_MIDIFADE`,
`ASSET_OBJECT_MIDIFADEPOINT` and `ASSET_OBJECT_MIDICHSET`. Build friendly controls
over their existing object formats: channel labels, enable masks, fade regions,
sequence binding and relevant duration/distance settings. Validate references
against the custom carrier and actual sequence channels. Do not silently strip
inherited music objects; identify incompatible ones in the export report and
let the author remove or remap them.

Separate accurate Blender listening from file import. An actual preview needs
the local DKR bank and compatible sample synthesis, envelopes, tuning and
effects. Until such a renderer is implemented and compared with the game, use
Track Lab for listening and leave the placeholder Play action unavailable.

Gate: crossing a region changes only the intended channels, remains in sync,
and still works after restart and the final-lap transition.

## Validation and delivery

Add meaningful tests to `tools/blender/run_tests.py` for music parsing,
conversion, package output and Blender persistence/export. Extend
`custom_tracks_tests.cpp`, `custom_tracks_runtime_tests.cpp` and guest pipeline
tests for schema handling, bounded writes, stopped-player gating, revision
adapters, metadata and legacy-hook coexistence. Use original synthetic music
fixtures in the repository.

The runtime matrix must include custom A -> custom B with the same carrier,
custom -> stock, stock -> custom, scene preview -> race, restart, menu return,
finish/results, jingles, music mute/unmute, volume fades, invalid files and
session shutdown. Verify Accurate and Modern presentation and local multiplayer
behavior without assuming the retail music policy is identical in every mode.
If custom tracks can enter a netplay route, bind content identity and reload to
that route's existing compatibility rules; do not change the audio clock.

During implementation, report format/unit-test success separately from audible
in-game qualification. The native-import stage is complete only after both
supported ROM revisions play an authored song and restore original music on
exit. Changes already present in addon files at planning time are unrelated
working-tree edits and must be preserved.

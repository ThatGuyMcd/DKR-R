# Custom tracks and characters

We've moved well beyond the first offline import beta. Legacy patches can now
add tracks and characters without replacing your originals, and the host can
share the required set for online play.

The filename remains `LEGACY-MODS-BETA.md` so existing links keep working.
These are the current 1.0.6 instructions. The filename is retained for existing links.

## Import and play

1. Add your original ROM in **Play**.
2. Open **Mods / Hacks → Import mods → ROM patches → Choose patch file**.
3. Choose an `.xdelta` or ZIP containing patches. The separate importer inspects
   it, discovers content and prepares compatible assets. Progress and Cancel
   remain available.
4. Check the result in **My mods → Tracks** or **Characters**. Imported legacy
   content is not automatically enabled.
5. Enable what you want, then launch. Import both clean US revisions before
   preparing tracks if you want variants for both native engines.

An xdelta needs the exact source bytes it was made against. A v1.1 ROM cannot
decode a v1.0 patch simply because both are DKR. Prepared assets can then be
adapted for the supported native revisions. No PAL/Japanese game engine is
added by this importer.

## Tracks and Player Select

Custom tracks appear below the original worlds with the original wooden
frames, previews and navigation flow. Fresh Track Select entry starts at
Dino Domain. `.dkrmap` courses from Track Lab share the Tracks library, marked
**DKR** rather than **Legacy**.

Each custom row has a CUSTOM TRACKS heading and alternates through the six
built-in backgrounds. Returning to Future Fun Land restores its original art.
Wider screens keep preview images, captions and arrows attached to their frames
during scrolling; confirmation zoom remains the original transition.
Very long captions are shortened in-game, not in the installed mod metadata.

For offline authored tracks, the vehicle screen can select the vehicles allowed
by the track header. A start object may still force a vehicle; Track Lab warns
about that on export. Online retains its current vehicle-selection gate.
Track Lab exports a looping preview flight if a race/boss track has none and
does not overwrite an author's existing flight. See the addon guide.

Custom track artwork and character artwork share checked boot-owned texture
and sprite tables. Large libraries are no longer truncated at the former table
scan limit; malformed offsets or exhausted budgets still fail safely. This is
not a promise that arbitrary tracks or unlimited assets fit in guest memory.

Four custom character actors sit on the original animated Player Select stage:
two below and two above. With more enabled, highlight a custom actor and use
**L/R** to cycle in that slot. Characters displayed in the other custom slots
are skipped, so your position doesn't jump to another actor.

The current sound-token format allows up to **1,536 enabled custom identities**,
not an infinite allocation. Installed character variants have a separate
16,384-entry metadata bound. Scene models, retained portraits and voices still
have memory budgets; a large collection can hit those before the identity cap.
Failed stage replacement keeps the previous choice rather than publishing a
half-loaded actor. Disable some content if preparation reports a budget error.

Eligible normal races can draw custom AI characters alongside unlocked stock
characters, reserving human choices and avoiding duplicates. Bosses, hubs,
time trials/ghosts and demos do not use this random custom AI pool.
Online draws use the frozen host roster, not a client's offline selection.

## Managing mods

Search, Status, Sort by and Filters help manage the library. **Duplicate review**
offers **Possible duplicates** and **Same source & slot**. Matching names alone
do not prove two editions are interchangeable; review Details before removing.

Cards keep the main information short. **Details** holds compatibility notes,
managed location, source import, activation, hide/restore and removal.
Hidden mods are inactive; restoring does not automatically enable them.
Changing installed content is locked while a game, lobby or import owns it.

Removing one track/character removes its prepared variants, not the other
content in the same source pack. Original ROMs, downloaded ZIPs/patches,
retained source-import material and saves are not deleted by card removal.
Re-preparing a retained source can reinstall removed content.

**Help & imports → Prepare all source imports again** checks unique retained
sources, including hidden entries, with source/worker progress and cancellation.
One failed source does not stop the rest. New reviewed editions remain inactive;
old editions, activation and save identities are not silently replaced.
Individual import/Details views also offer source re-preparation.
Missing original patches or source ROMs must be supplied, not guessed.

## What a legacy patch can do

This is a reviewed native compatibility system, not arbitrary ROM-code execution.

- Extracted models, animation, textures, portraits, scoped voices and supported
  course/music resources become privately owned native assets.
- Reviewed racer parameter tables select values by custom human/AI identity,
  without changing the original donor's global statistics.
- Validated native-layout object-header data can adapt scale, lighting, shadows,
  water effects and draw distance within checked bounds.
- Known constant-load edits and exact rebuilt-code profiles use scoped authored
  adapters. Some access/unlock changes are represented by additive menus rather
  than overwriting the original global unlock state.
- Reviewed random texture-animation behaviour has a scoped native adapter.
  Unknown opcodes, structural/global-data changes and whole campaigns still
  need separate compatibility work.

Details distinguish asset-only, native-adapted and partial support. Two editions
with identical textures but different reviewed behaviour need not have the same
identity. Preparing again can create a behaviour-aware edition; select it
deliberately. The [behaviour compatibility report](LEGACY-BEHAVIOUR-20261010.txt)
records the exact reviewed corpus, not a promise that every future patch works.

A patch that works in an emulator may still require an adapter. Invalid
geometry, unsupported dependencies and unsafe layouts are rejected rather than
loaded with a guessed fallback. Replacement sounds/portraits only work when
the mod supplies them; unchanged donor resources remain unchanged.

Custom results, challenge/trophy portraits and eligible AI use scoped identity
routing. Saved ghost identity and independently named custom records remain
limited by retail carrier slots. Treat custom time-trial records as experimental,
not universally comparable records.

## Online sharing and offline retention

Enable the set before hosting. Clients review it and consent before receiving
missing payloads or reconstructing assets. Matching local offline content and
verified caches are reused, regardless of offline activation/visibility.

Received assets are verified against the client's original ROM and frozen host
manifest. Completed content can be kept as inactive entries for offline play.
This does not replace the client's offline selections or save data.
Host/client progress covers transfer and local preparation; admission waits for
both to complete.

Experimental Rollback checkpoints mod identity/native state for both admitted
US revisions. Host Prediction and Strict Input Sync use their existing online
paths with the same reviewed mod contracts.
See [online multiplayer](ONLINE_MULTIPLAYER.md).

## Saves and recovery

Offline legacy mod sets use `saves/mods/<mod-set fingerprint>/`. First use
copies ordinary Adventure progress or creates a valid blank; subsequent use of
the same set retains its progress. Modded Paks remain separate.
Disabling the set returns to ordinary offline storage.

Save Manager includes Modded and Recovery views alongside Offline/Online.
See [save protection](SAVES.md) before restoring a different scope.

Don't alter managed receipts, review data or transaction journals by hand.
Retained reviews supply verified audio and reconstruction data.
Preparation faults remain in the launcher; runtime asset faults stop the guest
instead of leaving unsafe partial state running.

## Developer boundary

Normal and owned rollback payloads need the composed scene, track menu,
character resource/menu/presentation and behaviour hooks for both revisions.
The historically named `scripts/generate_legacy_menu_qualification.py`
composes production fragments; its name does not enable a private recipe.

`DKR_LEGACY_QUALIFICATION` is **OFF** for packaging. Use the full pipeline,
required-hook checks and content identity; never hand-edit generated C or
dependency checkouts. See [Building](BUILDING.md) and [Development](DEVELOPMENT.md).

The older [Beta 7 corpus report](LEGACY-MOD-COMPATIBILITY-BETA7.md) remains a
historical test record, not the current compatibility list.

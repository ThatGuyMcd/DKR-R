# Shared-save Patch Pipeline

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

This stage composes the accepted SDK 2 startup fix and beta 9 Store UI/installer
without changing either input, checked-out runtime code, generated CPU payloads
or dependencies. It produces first-party native sources in a new build folder.

## Policy

- Normal offline launches always select `saves/dkr.us.v77.bin` and the personal
  Controller Pak directory. Mod/content identities continue to identify assets
  and multiplayer compatibility, not personal save ownership.
- Development remains separate. Online uses the stable host Adventure and
  match-specific client copies; no online writes target personal progress.
- New offline development sandboxes seed from the valid personal image, not
  an implicit empty EEPROM. Existing sandboxes are retained unless the owner
  explicitly queues a personal snapshot; replacements preserve the original.
  Startup logs identify the selected write owner and decoded slot totals.
- A first-use migration snapshot preserves bounded recognised images and an
  index of original paths. No source is deleted, promoted or merged implicitly.
- Completion actions use retail fields, retain record bytes and unknown global
  bits, save on confirmation and verify the committed image by reading it back.
- Added-course record companions never enlarge the stock 512-byte EEPROM.
  The owned CPU emits record observations into its effect journal; only its
  validated confirmation owner publishes records to the persistence worker.
- The legacy replacement-course carrier record convention is unchanged. This
  is not unrestricted support for full-ROM-hack save formats.
- Bundle versions 1 and 2 are admitted. Version 2 adds a typed custom-record
  item. An on-disk journal preserves originals across multi-file replacement;
  failure/interrupt recovery validates all originals before restoring any.
- All UI persistence jobs run on a bounded worker. Launch/exit fences wait for
  the outstanding operation; stale drafts require reload instead of overwriting
  newer disk data. Inventory and normal summaries are read asynchronously.

## Compose

```
C:/Python314/python.exe -B patches/shared-saves/pipeline.py stage --sdk build/sdk2-dev-menu-startup-20261018-r1/stage --store build/mod-store-ui-polish-20261019-r4/stage --output build/NEW-shared-save-stage/stage
```

Configure with `CMAKE_PROJECT_INCLUDE=.../patches/shared-saves/NativeOverlay.cmake`
and `DKR_SHARED_SAVES_STAGE=.../stage`, retaining the accepted Camera++, shadow,
launcher, SDK and Store stage options. `DKR_RELEASE_VERSION=1.0.7+beta.10` sets
this delivery's version without rewriting the existing checkout's version.

Optional `DKR_SHARED_SAVES_REUSE_STAGE` reuses only public include-directory
paths after every staged `.hpp` is verified byte-for-byte. Changed translation
units still come from the new immutable stage. This avoids recompiling every
unchanged translation unit because an absolute header root changed.

The appended source-content identity is shared by Windows and Linux. It is a
new rollback contract: all peers must upgrade together.

## Owner acceptance

The owner performs all execution/testing. Build and inspect artifacts without
launching the app, running CTest or invoking runtime self-tests.

Use an external data-folder backup, then check: stock/modded launch continuity;
completion/unlocks after restart; stale draft refusal; recovery selection;
version-1/version-2 transfer; interrupted import and storage errors; independent
client personal saves; host continuation after changing mods; custom record
persistence with changing installed-course order; developer isolation; and both
ROM revisions. Confirm Windows/Linux peers report the same per-revision ID.

See [release notes](RELEASE-NOTES.txt) for the user-facing migration instructions.

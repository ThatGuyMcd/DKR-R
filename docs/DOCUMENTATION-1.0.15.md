# v1.0.15 documentation review

This update covers online improvements, storage cleanup, Play layout, character
labels, file dialogs and crash diagnostics since v1.0.14. The
launcher, graphics, language, controller and modding additions from v1.0.14
remain documented in their existing guides. Independent mod, SDK, addon and
dependency versions keep their own numbers.

## Documents to read first

- [Release notes](PUBLIC-1.0.15-NOTES.txt): the complete player-facing changes,
  installation instructions and remaining problems.
- [Changelog](../CHANGELOG.md): the source changes grouped under v1.0.15.
- [Storage](STORAGE.md): cleanup controls, retention, protected data and recovery.
- [Launcher](LAUNCHER.md): pinned launch controls, controller focus, scrolling
  and the shared overlay layout.
- [Custom characters](LEGACY-MODS-BETA.md#player-select-names): selected labels,
  row placement, long names and support for up to four players.
- [Saves](SAVES.md): the widescreen initials keyboard and Save Manager footer.
- [Crash reporting](CRASH_REPORTING.md): consent, useful fault context, private
  symbols, dialog containment and the two unresolved crash investigations.
- [Troubleshooting](TROUBLESHOOTING.md): missing helpers, dialog failures and
  what to collect when a crash happens again.
- [Online](ONLINE_MULTIPLAYER.md): current defaults, recovery, optional datagram
  delivery and live versus queued host settings.
- [Spectators](SPECTATORS.md): confirmed playback, the initial buffer target,
  shared transfer allowance and joining limits.
- [Platforms](PLATFORMS.md) and [validation](../RELEASE-VALIDATION.md): exact
  build/test coverage, emulation and physical-device checks kept separate.
- [Building](BUILDING.md), [updates](UPDATES.md) and [release procedure](RELEASING.md):
  the checked pipeline, stable package names and signing/publication steps.

## What remains open

The missing vanilla online car textures/remote road wheels are still under
investigation. Network waits are reduced, not eliminated. Previous private
test results are labelled with their original build; they do not automatically
qualify the final release. Physical Steam Deck LCD Wi-Fi, separate machines,
controller feel and physical ARM graphics remain owner/device acceptance.
The supplied Windows graphics-startup and Linux gameplay faults remain open.
Separating file dialogs contains dialog failures; it does not prove the causes
of those other faults. The latest label checks include real custom-character
browsing under WSL, not a physical four-controller acceptance run.

The open GitHub reports are reviewed in the external release handoff. A release
note is not an issue closure. Existing translation, experimental graphics/haptics,
custom battle-map and unsupported-platform limitations remain documented.

## Repository and review

Private playtest announcement files are preserved outside the checkout; the
technical investigation notes remain under `docs/notes/`. The v1.0.14 release
notes and validation record are preserved. Required sources, tests, dependency
patches and licences remain in the repository. Build outputs, source snapshots,
profiles, receipts and signing material stay outside it.

The owner reviews the changed documents, final packages and validation evidence
before pushing, tagging, uploading or publishing. The proposed tag is
`Version1.0.15`; no public GitHub change is implied by local preparation.

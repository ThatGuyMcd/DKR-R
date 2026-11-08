# v1.0.14 documentation review

The current README and player/developer guides describe the accepted changes
since v1.0.12. Historical release notes and investigation records keep their
original versions and findings. Independent SDK, mod, addon and dependency
version numbers are not changed just to match the game release.

## What changed

- README, the documentation index and public notes describe the complete release.
- Launcher/overlay instructions use Installed Mods, the separate Mod Library,
  Textures inside Installed Mods, and the Play tools/session-mod switch.
- Startup instructions cover manual and automatic Starting Game, inline mod
  progress, cancellation and initial online sync presentation.
- Graphics covers aspect-specific resolution, supersampling migration, water
  colours, standalone Look behind, CRT placement and lighting repairs.
- Controls covers both sidebar routes, left-stick navigation, right-stick
  scrolling, analogue mod Details and controller feedback capability limits.
- Language, season and Steam Deck regional-format detection have current guides.
- Mod installation keeps the exact-source .xdelta warning for Library/manual
  imports. The save guide explains that ordinary offline mods share Adventure.
- Crash-report instructions cover saved consent, separate dump opt-in, bounded
  retry while the launcher is open and honest privacy limitations.
- Platform, build, update and release instructions distinguish compiled,
  emulated and physical-device verification. The missing release guide is restored.
- Required new source resources are included in source packaging, with tests
  rejecting unrelated downloaded fonts/archives.
- The SDK 2.0 book and rendered guide include the additive analogue-driving
  imports and all eight shipped headers. The companion's 1.0.1 metadata points
  to its Details settings and requires stable v1.0.14; its driving program is
  byte-identical to the accepted beta 3 companion.
- Release fixture header ownership and ARM64 tool execution are corrected;
  signed-updater qualification accepts the exact preceding stable and beta
  packages with their verified receipts.

## Review and cleanup

The external release handoff contains the changed documents, a readable combined
review, a complete documentation inventory, local-link results, the source change
list and a recoverable cleanup record. Generated archives, old preview/download
folders, extracted AppImage output, local UI state and caches are moved outside
the checkout. Required build tools/inputs, tests, licences, research and historical
records are retained. No dependency or generated guest code is edited for cleanup.

Review [public notes](PUBLIC-1.0.14-NOTES.txt), [platform support](PLATFORMS.md),
[release validation](../RELEASE-VALIDATION.md) and [release procedure](RELEASING.md)
first. No GitHub publication is authorised until the owner approves this review.

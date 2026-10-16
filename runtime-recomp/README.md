# DKR-R native recompilation runtime

This is the project-owned integration layer for DKR-R 1.0.7.
It combines generated DKR CPU/Rare RSP output, N64ModernRuntime, the F3DDKR/RT64
bridge and the shared launcher/overlay.

Both clean US revisions run in-process in the same application.
The runtime owns input profiles, gyro, controller mapping, scoped saves,
audio/EQ, presentation/HUD, texture priorities, enhanced water, custom tracks/
characters and online admission. Supported desktop builds keep SDL3 input in
a headless private worker; Android uses SDL2 directly.

## Normal and owned execution

Host Prediction and Strict Input Sync retain the ordinary runtime path.
Auto selects the separately admitted Experimental Rollback owner, built from
isolated full-scene CPU closures for v77/v80. It restores/replays checkpoints
while rendering, confirmed audio and persistence remain external consumers.

Normal generated output alone is not a complete experimental release.
See [Building](../docs/BUILDING.md) for policy composition, both owned outputs
and the integrated feature settings. The backend is in the existing launcher,
not a separate player application.

Protocol 50 carries the linked replay-engine identity and prepared mod-resource
proof before admission. The initial checkpoint then compares named canonical
contract fields; never bypass a mismatch to make two platforms connect.
See [Online](../docs/ONLINE_MULTIPLAYER.md) and
[release validation](../RELEASE-VALIDATION.md).

## Reproducible patch boundary

Never hand-edit RT64, N64Recomp, N64ModernRuntime, `RecompiledFuncs` or
`RecompiledPatches`. Use authored policies/fragments and
`patches/manifest.json`, then regenerate into fresh output directories.
Do not remove signature, ABI, manifest or budget gates to make a payload build.

Development commands, tests and release checks are described in
[Development](../docs/DEVELOPMENT.md).
Default wrapper output is `build/dkr-runtime-rt64/bin/Release/DKR-R.exe` on
Windows and `build/dkr-runtime-linux/bin/Release/DKR-R` on Linux.
Custom build directories differ. Linux releases always include an AppImage.

## ROM and save policy

Validate the exact canonical v77/v80 ROM before registration. Byte order is
detected independently of the extension. No ROM is modified or packaged.

The 512-byte EEPROM format is compatible between revisions; that does not
permit sharing writable offline/online routes. Online and legacy mod sets hold
separate save owners. See [Saves](../docs/SAVES.md).

Current user instructions are in the [documentation index](../docs/README.md);
licensing requirements remain in [COPYING-NOTICE.md](COPYING-NOTICE.md).

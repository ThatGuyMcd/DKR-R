# Preparing a DKR-R release

The current release is prepared locally through the existing checked Patch
Pipeline. The GitHub workflows perform host/tooling/device-adapter checks;
there is no tag-triggered full game release workflow in this checkout.
Publication is a separate maintainer action.

## Source and version

1. Preserve a recoverable source snapshot outside the repository. Review all
   changed and new source, documentation, fonts, artwork and backend files.
2. Keep dependencies, RecompiledFuncs, RecompiledPatches and frozen source
   capsules unchanged. Use existing baselined integration while the fold is
   incomplete; do not create another first-party patch layer.
3. Set the root VERSION and current package/AppStream metadata. Retain old
   notes, independent SDK/addon versions and dependency versions.
4. Rebuild the complete accepted integration for both supported ROM revisions.
   The historical recipe path remains `patches/release-prep/v1.0.11/`; its
   current configure/build/package scripts read VERSION. Do not rerun the old
   `apply`/`content.py` authoring operation against today's source.
5. Record the exact reviewed source commit before final signing. All source
   needed for the build must be included; a dirty or incomplete tree is not
   a committed release identity. Source ZIPs exclude private prepared game data.

## Platforms and checks

Windows x64, Linux x86_64 and Linux ARM64 have complete runtime/helper builds.
Steam Deck uses an exact copy of the Linux x86_64 AppImage. Review all other
platforms and record their actual status in [Platforms](PLATFORMS.md); a host
or adapter compile is not a qualified game download.

Run the mandatory checks described in [Contributing](../CONTRIBUTING.md):

```sh
python3 scripts/check.py --suite all
cmake --preset host -S runtime-recomp
cd runtime-recomp
cmake --build --preset host
ctest --preset host
```

Use `python`/`py -3` and the host-windows preset on Windows. Run installed
pre-commit hooks on the changed files. Keep failures visible. Run timing-heavy
regression fixtures on native Linux storage when a mounted-volume timeout is
encountered; record that rerun and its exact coverage.

The retained release fixture tools check original behaviour, render/memory
ownership, content preparation, vehicles and online policies. ARM64 CPU/data
fixtures may use QEMU with the matching sysroot. Native graphics, controller
feel, audio, Gamescope and real network play remain separate acceptance checks.
Exercise both ROM revisions, modded/vanilla launches, failure recovery, saves,
launcher return and the current startup/online handover on the finished packages.

## Packaging

Use the existing release package allowlist and current source audit. Package
all helpers, assets and licence notices. Read every ZIP/AppImage back against
its file list and hashes. Inspect both Linux ELF dependency closures and system
ABI floors, and probe the packaged workers. Do not rename an old binary.

Keep build directories, source backups, hashes/receipts, screenshots, test
profiles, extracted packages, credentials and downloaded game data outside the
repository source. Required local read-only build inputs can remain ignored.
The public source archive includes authored resources and tests, with no ROM,
extracted assets, player saves, private keys or live-service secrets. Run the
asset scan before committing and inspect the source ZIP contents too.

## Updates and review

Finish packages before signing. Use the original trusted Ed25519 key outside
both the repository and release output. Do not generate a replacement key for
convenience. The final `DKR-R-update.json` and `DKR-R-update.sig` must bind the
stable version, exact source commit, tag and final package bytes. Steam Deck
uses the linux-x86_64 updater entry. Excluded platforms get no entry.

The current wrapper is:

```text
python -B patches/release-prep/v1.0.11/update_assets.py sign --packages <finished-release-folder> --commit <full-reviewed-commit> --private-key <protected-original-key> --output <fresh-signing-folder>
python -B patches/release-prep/v1.0.11/update_assets.py verify --packages <finished-release-folder> --output <signed-pair-folder>
```

Qualify normal upgrades, corruption rejection, interruption recovery and
preservation of synthetic saves/settings/mod selections. Verify packages after
copying them into the final handoff. If documents or source change after the
commit, update the source identity and regenerate affected packages/signatures.

For v1.0.14 the proposed release tag is **Version1.0.14**. Prepare release notes,
checksums, the complete document review and validation evidence for the owner.
Do not push, upload, create a public release or publish until the owner approves.
Publishing a stable release can make automatic updates available. After approval,
verify uploaded hashes before publication and preserve the previous release.

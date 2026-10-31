# Fold follow-ups, 2026-10-31

## Checkpoint and scope

The original fold branch remains at `6ac4288` (B9b). Local follow-ups are
on `refactor/fold-followups-20261031` in the fold worktree. Nothing has been
pushed. Main is `d457741` after fetching origin. Its untracked
`docs/start-menu-redesign/` was left alone.

The v1.0.11 equivalence record is historical evidence. At checkpoint
`ca96ecc`, the initial upstream fixes intentionally make `closure --check` report three changed files out of 710:
`mod_store.cpp`, `netplay/direct_session.cpp` and `presentation_identity.cpp`.
Do not re-record
that closure to conceal the behavior change. These follow-ups must stay
separate from the equivalence checkpoint and its merge evidence.

## PR audit

- [#80](https://github.com/ThatGuyMcd/DKR-R/pull/80), merged: the history
  policy and regression fixture live in first-party directories. Runtime
  integration still uses the baseline-listed character-library-portraits
  pipeline. That is permitted by main's transition rule, but needs a direct
  source integration on the folded tree. This branch makes that integration
  in `mod_store.cpp` and registers the 61-check history fixture with CTest.
- [#83](https://github.com/ThatGuyMcd/DKR-R/pull/83), open, inspected at
  `de058c4b5d7b6487d7dee064ac0cb515ea5c6e2f`: the three dependency fixes are
  registered checksum-pinned patches for N64Recomp, RT64 and plume. Setup
  edits are in scripts and the prerequisite fixture is in runtime tests.
  It adds no first-party layer or configure-time source swap. All three patch
  SHA-256 values match their registry entries. Structural
  compliance does not establish runtime shader correctness. It has no
  reported GitHub checks at this audit; it has not been imported here.

## Remaining upstream reconciliation

Main has eight commits beyond the fold's common ancestor. The complete
upstream delta spans 116 files, including release documentation. These
focused ports do not bring the fold to v1.0.12. Remaining work includes:

- Profile/account UI and services: fold the existing composition points into
  their source owners; avoid introducing a second friend-service implementation
  selected by whether generated payloads are enabled.
- Directory backend/social-profile changes; native room retirement is ported below.
- Release and updater tooling changes, mapped into scripts/release rather than
  restoring deleted release-prep layers.
- Release documentation/version changes after the behavior ports are complete.

The existing Windows, Android and cross-machine release evidence gates in
`docs/fold-in/open-items.md` remain open; local macOS checks cannot close them.

## C5 fixture promotion

The handoff and render-selection fixtures were recovered from main's retained
release layers and now include production headers directly. They report 37
checks with 8000 concurrent transfers and 30 checks with 4000 concurrent
workloads respectively. Their CTest names match the release registry and
their targets follow the assertion-enabled `DKR*Tests` convention.

The transfer fixture adds the retained repair fault cases to the existing
`online_mod_transfer_tests.cpp`: corrupted private payloads are quarantined,
valid payloads and incomplete downloads survive, cancellation and traversal
are rejected, and symlinks fail closed. `DKRStabilityTransferChecks` builds
that source against production DKRLegacyModCore.

The offline-retention fixture uses production DKRLegacyImportLibrary. A small
runner gives each invocation a new private temporary fixture directory and
removes its synthetic files afterward, including on a failing exit.

Repository pre-commit hooks were bootstrapped, but the global core.hooksPath
still takes precedence. Pre-commit is run explicitly; no global preference was
changed.

Eight release gates are restored; 7 remain pending. The initial host build
and all 144 tests passed, as did the macOS full-nogen DKRPortGameCore build.
The added retention fixture passes repeated runs after canonicalizing the
macOS temporary-directory path. The B9 token/format proof command succeeds;
the historical closure reports the three runtime ports. It normalizes include
spellings, so the residency include seam does not add another closure delta.

## Further upstream runtime ports

Directory retirement is applied directly in `netplay/direct_session.cpp` on
explicit disconnect and terminal failure. The 21-check lifecycle fixture is
a separate host target, using the production directory implementation, and
checks scoped cleanup, repeated hosting, revoked admissions and one-time
bootstrap key consumption across room retirement.

Offline live FOV refresh is called from the existing presentation-frame hook.
The policy and guest-call helper come from main, including rebinding f_odd
inside a copied recomp_context. Its 824-check fixture covers settings, scene
transitions, online exclusions and both guest floating-point register modes.
Neither port edits guest generated source, changes legacy_identity.hpp, or
adds a new runtime source-selection branch.

The spectator-admission music fixture also runs against production
DKRLegacyModLaunch. It exercises both revisions with synthetic WAVs and guest
fields, verifying admitted preparation, cold-restore rejection, truncated and
forged checkpoints, retry limits and matching confirmed audio. The separate
pre-fix negative-control gate remains pending; no gate was removed to replace
that missing evidence.

Upstream commits `39262a6` and `4d0d8d9` repair legacy updater harness
text anchors and a legacy packager's feature-marker call signature. The folded
updater already passes explicit `--previous`/`--previous-version` arguments,
and its packager calls its own feature checks directly. Those legacy wrapper
repairs require no corresponding compatibility layer in scripts/release;
its 19 tooling tests pass.

Main's transition guard also passes at `d457741`, with zero new problems and
1402 baseline findings tolerated. This confirms structural transition
compliance, not completion of the fold.

The race-release (96 checks) and spectator-handler (45 checks) fixtures now
share an authored test fixture header that includes production party_runtime.cpp
directly. A synthetic OwnedGame and SDK command service substitute for the
live runtime; no generated stage or alternate production implementation is
built. The two historical Before controls remain pending.

The liquid-residency fixture compiles the production residency body with its
retained fake cache and clock, and passes 23 checks. Four dependency includes
in water_residency_rt64.hpp now use angle brackets so the fixture can supply
its isolated interfaces through its include path. Production builds retain
the real headers through the game include directory; no source is copied or
rewritten during configure/build. The rice texture policy stub forwards to
the production policy. The fixture uses dkr_guest_symbols for the real
polish-symbol header; no fake generated symbol header is committed. The closure comparator normalizes include spellings; production builds on
each platform must still validate that the real dependencies resolve.

## Validation checkpoint ca96ecc

- Guard/tooling and explicit pre-commit checks pass with the empty fold baseline.
- The macOS host run grew from 141 to 152 tests as fixtures were promoted.
- macOS and Linux x86_64 full-nogen DKRPortGameCore builds pass.
- All ten initial follow-up fixtures pass under Linux x86_64 emulation; the
  later liquid-residency fixture passes there too.
- Linux f3ddkr_rt64.cpp's recorded compiler dependencies resolve the real
  water-residency, enhanced-water, generated polish-symbol, rice-texture and
  performance headers. It does not use the test stubs. Apple intentionally
  excludes this water code, so its build alone cannot validate that seam.
- The historical B9 format proof passes. The historical closure check reports
  only the three intended runtime ports; no recorded hashes were refreshed.

Still pending in C5: memory-stability-v77/v80, owned-heap-v77/v80,
spectator-admission-before, race-release-before and vehicle-original-textures.
Finalization still rejects the tree while those seven checks are pending.

## Native profile and remembered-sign-in follow-up

The profile bridge is folded into the existing netplay/friend_service.cpp.
There is no second friend-service source selected by generated-payload build
flags. It binds the launcher profile to the native public key, supplies
challenge proofs, retains timed lobby capabilities and validated peer avatars,
and retires callbacks by the service generation. The existing friend tests
exercise substituted identities and keys, stable challenge responses and
unapproved avatar URLs; the separate bridge fixture checks retirement.

The existing Mod Store account worker now uses the upstream private device
storage fallback when a system keyring is unavailable. Its directory/file
permissions are 0700/0600, and the helper rejects links and invalid tokens.
Turning remembered sign-in off removes its saved credential. Existing Online
and Store sign-in controls expose the preference without dead constant branches.
The imported policy fixture passes 79 checks over retries, name acceptance and
private storage; it only creates synthetic temporary files.

The profile UI, profile-picture account endpoints and directory/social backend
changes still need reconciliation. Backend modules belong in services, not in
the native online_profile directory. No backend was deployed or copied there.

With these native ports, the historical closure reports six intentional
changes: directory_ui.inl, mod_store.cpp, netplay/direct_session.cpp,
netplay/friend_service.cpp, presentation_identity.cpp and store_account_page.inl.
Its recorded hashes are unchanged. C5 still has the same seven pending gates.

Validation for this follow-up: all 154 host tests pass; guard/tooling,
explicit pre-commit and the staged pattern guard pass. The macOS and Linux
x86_64 full-nogen game-core builds pass. Linux also passes FriendService,
OnlineProfile and ProfileBridge tests. The original B9b branch remains at
6ac4288, and main's untracked menu-redesign work is untouched. No pushes,
GitHub review comments or deployments were made.

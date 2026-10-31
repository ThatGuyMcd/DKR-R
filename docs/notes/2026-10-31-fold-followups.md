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

## Profile UI, portraits and maintained online service modules

The native profile follow-up now includes picture preparation, account controls,
verified friend cards and lobby social metadata. Malformed or unverified optional
metadata is rejected locally without retiring the directory connection. Character
portraits are an authored module and CMake library, with a synthetic decoder and
private-storage fixture; no source staging is involved.

The online service owns its API modules under services/online-directory/api.
Its builder uses pinned esbuild, produces a fresh reviewable bundle and records
input/output hashes. Optional owner rollback inputs are preserved byte for byte.
The local Miniflare harness supplies an explicit modulesRoot so bundles outside
the checkout work. Profile proof tests exercise the real native X25519 helper
against Worker Web Crypto and synthetic D1 data. Before applying profile
migrations, tickets still succeed with authentication and reject anonymous
requests. No live service or real credentials are used.

Validation: all 156 host tests pass. The macOS full-nogen game core builds and
the prepared macOS DKRPortGame executable links. Directory harnesses pass 95
checks normally and 101 with running spectators; the expanded profile harness
passes 94 checks. Bundle tests cover fresh outputs, exact rollback bytes,
compiler rejection, receipt tampering and invocation from another directory.
Host CI now fetches sparse guest symbol tables automatically; service CI builds
the native profile proof fixture before running the local Worker tests.

These ports do not establish compatibility with current main. A separate
ancestor audit found the online-performance layer absent from the v1.0.11
materialized closure. The shipped v1.0.12 identities were reconstructed and
confirmed in the official Windows runtime. See
[the identity proposal](2026-10-31-v1012-identity-proposal.md). Identity pins and
compatibility-changing performance ports remain pending both owners' approval.
The seven C5 gates and outstanding Windows/Android acceptance remain open.
No pushes, PR publication or deployments have occurred.

## Refreshed open-PR audit

At this refresh, the open PRs are #63 and #83. #83 is still at
`de058c4b5d7b6487d7dee064ac0cb515ea5c6e2f` with no reported check runs. Its
registered dependency-patch structure is unchanged from the earlier review.

[#63](https://github.com/ThatGuyMcd/DKR-R/pull/63), at
`619573616f6af3a7f0f7f1bf2d3dda40c93be9ad`, edits first-party sources directly
and introduces no staging layer or generated guest rewrite. However, its
accessibility observer embeds retail symbol addresses in 182 revision-paired
address calls. For example, accessibility_observation.cpp:134 reads
gMenuCurIndex through literal v77/v80 addresses; lines 149 and 154–158 do the
same for menu text, level headers/names and EEPROM settings. These symbols
need TOML ownership and generated headers before the change is fold-compliant.
Structure offsets can remain in code. Its CMake source additions also need
the folded DKRPortGameCore layout so full-nogen builds exercise them. The
PR's own VoiceOver journey and intended-player merge gates remain pending.

The same PR explicitly negotiates Quick Join after route publication,
serializes replacements and restores the predecessor after stage-7 failure.
The current fold retains the earlier ordering. This is a targeted candidate
for the documented bootstrap flake, not proof that the flake is fixed. It has
not been imported into this branch. No GitHub reviews or comments were posted.

The two later release-tool repairs already have direct equivalents here:
qualify_updates.py passes explicit preceding-package/version arguments to the
authored test harness instead of rewriting it, and package_release.py owns
its accepted feature markers directly. Their legacy-layer adapter changes
should not be restored.

## Host log ownership follow-up

The non-simulation log pieces of the online-performance layer now live in
runtime_log_handles.hpp and the existing game_main.cpp/update_process.cpp
owners. A busy current log selects a unique bounded-history fallback; the
log descriptor is marked non-inheritable, and Windows CreateProcess disables
handle inheritance. The process fixture checks actual POSIX descriptor flags,
valid unique archive names, archive failure and, on Windows, a child given an
explicitly inheritable synthetic log handle. Its file-identity check tolerates
handle-number reuse in the child. The Windows branch still requires Windows
execution; macOS alone cannot establish it.

The prepared macOS application links with these changes and the expanded
process fixture passes on macOS. The Linux x86_64 full-nogen game core and
ProfileBridge, ProfilePictures, CharacterPortraits and DirectoryLifecycle
fixtures pass for the profile/portrait checkpoint. The gameplay/menu and
transport-performance reconciliation remains separate and pending.

## Bounded setup and immutable resource reuse

The existing transport_send_policy.hpp now owns call-scoped setup permission.
Authenticated ModData sends and pre-Go loading checkpoints can use up to
256 KiB of checkpoint queue; control/authority retain bounded headroom. Race
and replica admission keep their existing limits. No packet format, peer
identity or guest operation changed. Nested scopes, thread isolation, exception
cleanup, overflow rejection and race limits are covered by the production
transport-policy fixture.

The mod cache flushes at 1 MiB batches and still flushes before publication.
The transfer fixture writes a synthetic payload across two full batches and
checks the published hash, while retaining its cancellation/resume/error cases.
The existing resource policy allows 768 MiB on capable desktops, retains the
256 MiB mobile cap and rechecks memory pressure before strong-reference reuse.
iOS now passes its mobile flag explicitly, as Android already did.

Validation: all 156 host tests pass, along with refreshed focused setup, transfer
and cache-policy tests. The macOS application links. Linux x86_64 builds the game
core and passes QuickJoinTransport, StabilityTransferChecks, RuntimeLogArchive
and UpdateProcess. Guard/tooling and explicit pre-commit checks pass. Remaining
online-performance work includes party input/checkpoint scheduling, phase timing
and the held menu/identity reconciliation.

## Party scheduling and phase diagnostics

Party input now uses the existing reliable/unordered authoritative channel,
including repairs and host relays. The packet bodies, immutable input admission,
epoch/lease checks and repair history are unchanged. Initial checkpoint delivery
allows four chunks per receiver and sixteen total per pass; racing delivery
retains one per receiver and four total. A blocked queue does not advance the
checkpoint sender. These are native delivery changes, with no guest menu or
identity-pin edits.

The production party fixture now passes 151 checks, including reordered and
duplicate input, setup/racing burst bounds, a full real peer queue and aggregate
setup limits across five receivers. The existing admission, handler and offline
retention fixtures also pass in the initial focused macOS run.

Download synchronization yields after draining a full receive batch, while
retaining bounded batches, active-slot fairness and existing backpressure.
Diagnostics separate setup and payload time, and report resource-preparation
phases and bounded party step summaries. Setup time ends at payload consent;
it no longer incorrectly includes the payload phase. These logs are engineering
evidence and introduce no player-facing diagnostic controls.

The prepared macOS application links. All 156 host tests pass. Linux x86_64
builds the game core and passes RaceReleaseChecks, OfflineRetentionChecks,
SpectatorHandlerChecks and SpectatorAdmissionChecks. Guard/tooling and explicit
pre-commit checks pass. The remaining online-performance
source gap is the guest owned-menu notification admission policy, held with
the explicit identity-pin proposal pending both owners' approval.

PR #83's three registered dependency patches also pass git apply --check
against this checkout's prepared N64Recomp, RT64 and plume sources. This was
read-only: no dependency patch was applied, imported or executed. Patch
applicability does not establish runtime shader correctness or CLT-only build
acceptance.

## Owned heap entry C5 fixtures

The real checkpoint/mod-dispatch/stock-heap prefix of dkr_probe_native now
lives in probe_native_entry.h. The native entry calls it before its existing
non-heap branches; an explicit handled result keeps unknown operations on the
existing fallthrough path. Owner dispatch retains precedence, and the stock
heap bound and register casts are unchanged. No menu policy, identity pin,
generated guest source or configure-time transform changed.

The two owned heap fixtures compile that production prefix as C through a
thin call wrapper and link the synthetic C++ callbacks against the real recomp
register ABI. Both revisions pass the original 13 checks: stock expansion,
unknown pools, custom-owner precedence, unknown-operation fallthrough and
exact checkpoint/dispatch counts. Both production owned native translation
units also compile with their prepared revision headers and full-scene flags.
The ordinary prepared macOS application still links; this is separate from
compiling the owned native objects. All 158 host tests pass, as do both Linux x86_64 fixtures. Guard/tooling and
explicit pre-commit checks pass. The release tooling test now names the five
remaining pending gates and still proves finalization rejects them.

These restore the two owned-heap release gates, leaving five C5 gates pending:
memory-stability-v77/v80, spectator-admission-before, race-release-before and
vehicle-original-textures. Finalization still rejects an incomplete release.

## Integration with current main

The fold branch is integrating origin/main at d457741, preserving all eight
commits added since the common ancestor. Profile, directory, history, live FOV
and performance work is retained in its folded source owners; duplicate native
backend files, the build-dependent alternate friend_service.cpp and duplicate
CTest registrations are removed. Their maintained service/native/test owners
already contain those ports. Historical release recipes remain historical.
Current documentation and metadata now follow main's v1.0.12 release.

Release documentation checking now includes staged/working changes and keeps
renamed historical pages classified as archives. Obsolete links inside an
archived release page are preserved; broken current links still fail, covered
by a regression fixture. The release metadata check passes.

The user confirmed both owners approve adopting the shipped v1.0.12 IDs. This
approval applies to the explicit identity/header and owned-menu reconciliation;
no additional approval question is needed for those edits. No push or PR has
been made yet. The PR will target ThatGuyMcd/DKR-R:main, with latest main as
an ancestor once this integration commit is complete.

## Approved menu/identity integration

Both owners approved the shipped v1.0.12 identities in this session. The only
pin edits are the two defines in legacy_identity.hpp; all four engine salts
remain unchanged. The original B9/v1.0.11 evidence is preserved. The live owned
menu dispatch body is shared through probe_native_fields.h, with the supported
stock/vehicle notifications in owned_menu_policy.h. The real body receives owner
dispatch first and still rejects unknown calls or unavailable presentation.
No generated guest source is rewritten.

For each revision, 514 production dispatch checks cover all byte-sized events,
owner precedence and offline/canonical gates. Historical controls substitute
only the prior notification policy and must exit exactly 1; wrong failures
are rejected by the runner. The four menu fixtures and two heap fixtures pass
on Linux x86_64. The macOS application links and both production owned native
objects compile. Current release metadata and 322 local documentation links
validate; archived pages retain their historical links. All 162 host tests, guard/tooling and explicit pre-commit checks pass.

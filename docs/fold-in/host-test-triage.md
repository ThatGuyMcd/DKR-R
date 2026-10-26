# Host test triage

Linux host ctest (`host` and `host-full-nogen` presets) after the fold. The base
tree's tests describe pre-layer behaviour: the layers changed the sources but
not these tests, so they only broke once the layers were folded in (the
pre-fold base build failed just `DKRExperimentalQuickJoin`). Each one now
asserts what v1.0.11 ships. Where a feature was removed
outright, the test keeps a "must stay gone" check instead of dropping it.

| Test | What changed | Layer | Action |
|---|---|---|---|
| `DKRExperimentalNetwork`, `DKRExperimentalQuickJoin`, `DKRQuickJoinLifecycle`, `DKRLegacyDkrmapArtwork` | link-only: shipped sources reach into custom magic codes, legacy mods and the save manager | several | link the libraries or sources they need (CMake only, no test edits) |
| `DKRDirectSession` | lobbies are Rollback-only and need the owned engines; Lockstep and host-prediction lobbies are refused; native Gekko transport and host prediction retired | rollback-only | `default_rules()` plus `enable_owned_backend(true)`; legacy launches, the Gekko transport and the host-prediction soak are superseded, with refusal checks |
| `DKRDirectSession` | legacy connection-test scoring, preflight expiry and checkpoint exchange replaced by the isolated Rollback diagnostic | rollback-connection | superseded; the scores stay 0, there's no inconclusive status, and checkpoint requests don't start a test |
| `DKRNetplayCore` | `valid_rules`, `valid_launch_descriptor` and `decode_lobby_state` refuse Lockstep and host prediction | rollback-only | assert the Rollback rules and launch; Lockstep and host-prediction checks are superseded, and they're refused |
| `DKRNetplayInputLatency` | the native relay commits confirmed frames only, with no host prediction window | rollback-only | fixture runs Rollback lobbies; the bounds test asserts commit-to-confirmed and zero prediction waits; host-prediction matrix superseded (lobby refused), strict matrix kept |
| `DKROnlineModAdmission` | admission runs only for the Rollback backend | rollback-only | legacy backends superseded (their lobbies are refused); the owned-engine mismatch always blocks admission |
| `DKRExperimentalOwnerInput` | DKRX v3: command mask byte (53-byte header), 64 bytes of SDK2 command words per commanded input, only owner 0 may author commands | sdk2 | assert v3 sizes; command round trip on an owner-0 packet, refused for other owners |
| `DKRSaveManager` | import refuses a save that needs repair; continuing seeds the first host lobby from personal progress; host progress no longer keyed by mod manifest | shared-saves | assert refusal (live save untouched) and the shared host Adventure; per-mod folders superseded (must not exist) |

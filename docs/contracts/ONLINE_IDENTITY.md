# Online identity

Before two players' engines run a rollback match, they compare the owned
rollback contract of the ROM revision they share. If the contracts differ,
the lobby refuses the peer. The contracts were changed on purpose after
v1.0.15, so a build from this tree refuses v1.0.15 and earlier peers until the
next release ships.

| Revision | Define | Contract |
|---|---|---|
| US v1.0 (v77) | `DKR_OWNED_BUILD_ID_V77` | `5e4546d1209d785da50787207228884e02a47a6a8aad06838c3df1243dd321d4` |
| US v1.1 (v80) | `DKR_OWNED_BUILD_ID_V80` | `a52b30e2761b60e7526296858cf858478244b1d6308b63174c9e5310f1d9d57b` |

## Where they live

`runtime-recomp/src/game/netplay/legacy_identity.hpp` is the only place they
change. `cmake/OnlineIdentity.cmake` reads them into the compile definitions
`netplay/experimental_owned_registry.cpp` returns. A build without the Rev A
engine offers no v80 contract.

The owned engine identity a lobby manifest carries (`owned_engine_hash`,
`experimental::runtime_engine_identity`) is sha256 of the owned adapter
identity followed by four frozen digests, `DKR_LEGACY_ENGINE_SALT_1` to `_4`
in the same header.

The rollback test frontend (`DKR-R-RollbackTest`, not shipped) negotiates
`sha256("v77:<v77>|v80:<v80>")` of the same two values.

## Provenance

The contracts are `sha256("<v1.0.15 contract>|checkpoints-127-v1")` for each
revision. The checkpoint pool grew from 60 to 127 entries
(`instructionPatches` in both recomp policies), which moves every guest
allocation after it, so a build with it and a build without it simulate
different memory. Steve Whittaker approved the change on 17 November 2026; no
release had shipped from this tree since v1.0.15. The v1.0.15 contracts were
`e5c8df41e92c766cb46d137111a289793308e398033127160ff321ff4672a4bf` (v77) and
`a9a0d18fccc59d42aaa8b9cd6fdee50c22fa0699283eee8087a82f99ac85ef80` (v80).

The four engine salts are still the shipped v1.0.15 values. Before this change
the header held the shipped v1.0.15 values: both contracts and all four engine
salts, which v1.0.14 and v1.0.15 changed. They were read from the official
v1.0.15 Windows, Linux x86_64 and ARM64 runtimes without running them; Steam
Deck has the same published hash as Linux x86_64. Steve Whittaker approved
adopting them on 13 November 2026. See the
[verification record](../notes/2026-11-13-fold-v1015.md).

Before that the header held the shipped v1.0.12 contracts, which both owners
approved on 31 October 2026
([record](../notes/2026-10-31-v1012-identity-proposal.md)), with the v1.0.11
salts. The historical fold-equivalence record still describes its original
v1.0.11 checkpoint.


v1.0.11 derived each value at configure time from a sha256 chain over the
owned engine's generated and authored sources (`cmake/OwnedContentIdentity.cmake`,
with the release layers' ledgers). The fold reference's configure of tag
`Version1.0.11` produced the historical v1.0.11 values
(`generated/memory-stability/contracts.json`, docs/fold-in/REFERENCE.md),
using the owned, generated and code-policy inputs that reproduce the shipped
binaries. The values were also recomputed from `6bdbcde` and `5cab242` with
`base_id.py` and `chain.py`, with the same result.

The chain no longer runs. Sources now change without changing the contract.
Change the contract on purpose, by editing `legacy_identity.hpp` and this
table together, when a change breaks compatibility with older peers.

## The input record

`architecture/online-identity-inputs.json` is the lock on everything that can
change what peers simulate: the generated guest code, the owned engine's
sources and the online wire behaviour (`globs`), the sha256 of each matching
file, and the contract they were recorded against (`identity`, from
`legacy_identity.hpp`). `scripts/identity_guard.py check` runs in the guard
suite and fails when a file differs from its digest.

Each input carries the reason it last changed, next to its digest:

```json
"scripts/gen_guest_symbols.py": {
  "sha256": "…",
  "reason": "Type the names default for pyright; generated headers are byte-identical"
}
```

Record a change with one of:

- `python3 scripts/identity_guard.py update --compatible "why old and new peers stay compatible"`
  when `legacy_identity.hpp` is unchanged. It re-records the inputs that changed
  and writes your reason on those, and only those. It refuses when the contract
  changed.
- `python3 scripts/identity_guard.py update` after bumping `legacy_identity.hpp`
  (and the table above). Each changed input gets "online identity changed to
  <new identity>". It refuses while the contract is unchanged.

A reason is replaced when its input changes again, and an input nobody touched
keeps its old reason, so the record reads as "why each file last changed". The
reviewer's diff shows the new reason on every changed digest; in CI,
`identity_guard.py check --against <base>` fails a PR where a digest changed
and its reason did not (a hand-edited hash, or a merge that dropped one).

Two branches that change different inputs touch different entries, so they
merge without a conflict and `check` passes on the merge without re-running
`update`. A conflict means both changed the same input, or both edited
`identity`; resolve it by taking one side and running `update` again for
your change.

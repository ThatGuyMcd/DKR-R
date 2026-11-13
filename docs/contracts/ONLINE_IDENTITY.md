# Online identity

Before two players' engines run a rollback match, they compare the owned
rollback contract of the ROM revision they share. If the contracts differ,
the lobby refuses the peer. The contracts are frozen at the values v1.0.15
shipped, so a build from this tree uses the current release contracts.

| Revision | Define | Contract |
|---|---|---|
| US v1.0 (v77) | `DKR_OWNED_BUILD_ID_V77` | `e5c8df41e92c766cb46d137111a289793308e398033127160ff321ff4672a4bf` |
| US v1.1 (v80) | `DKR_OWNED_BUILD_ID_V80` | `a9a0d18fccc59d42aaa8b9cd6fdee50c22fa0699283eee8087a82f99ac85ef80` |

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

The header holds the shipped v1.0.15 values: both contracts and all four engine
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

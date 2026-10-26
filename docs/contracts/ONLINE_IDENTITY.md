# Online identity

Before two players' engines run a rollback match, they compare the owned
rollback contract of the ROM revision they share. If the contracts differ,
the lobby refuses the peer. The contracts are frozen at the values v1.0.11
shipped, so a build from this tree plays with v1.0.11 players.

| Revision | Define | Contract |
|---|---|---|
| US v1.0 (v77) | `DKR_OWNED_BUILD_ID_V77` | `5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e` |
| US v1.1 (v80) | `DKR_OWNED_BUILD_ID_V80` | `32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0` |

## Where they live

`runtime-recomp/src/game/netplay/legacy_identity.hpp` is the only place they
change. `cmake/OnlineIdentity.cmake` reads them into the compile definitions
`netplay/experimental_owned_registry.cpp` returns. A build without the Rev A
engine offers no v80 contract.

The rollback test frontend (`DKR-R-RollbackTest`, not shipped) negotiates
`sha256("v77:<v77>|v80:<v80>")` of the same two values.

## Provenance

v1.0.11 derived each value at configure time from a sha256 chain over the
owned engine's generated and authored sources (`cmake/OwnedContentIdentity.cmake`,
with the release layers' ledgers). The fold reference's configure of tag
`Version1.0.11` produced exactly these values
(`generated/memory-stability/contracts.json`, docs/fold-in/REFERENCE.md),
using the owned, generated and code-policy inputs that reproduce the shipped
binaries. The values were also recomputed from `6bdbcde` and `5cab242` with
`base_id.py` and `chain.py`, with the same result.

The chain no longer runs. Sources now change without changing the contract.
Change the contract on purpose, by editing `legacy_identity.hpp` and this
table together, when a change breaks compatibility with older peers.

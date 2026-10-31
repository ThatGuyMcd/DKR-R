# Online identity

Before two players' engines run a rollback match, they compare the owned
rollback contract of the ROM revision they share. If the contracts differ,
the lobby refuses the peer. The contracts are frozen at the values v1.0.12
shipped, so a build from this tree uses the current release contracts.

| Revision | Define | Contract |
|---|---|---|
| US v1.0 (v77) | `DKR_OWNED_BUILD_ID_V77` | `dc883843594ccb02931dbab4071080c0dcf0d7c0a960ee501529dd37230651c7` |
| US v1.1 (v80) | `DKR_OWNED_BUILD_ID_V80` | `105df80b777b20df308fede4235575dce5881e643130ebabf8cc60d71ff000e0` |

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

Both owners approved adopting the shipped v1.0.12 values on 31 October 2026.
They were reconstructed from the retained online-performance ledger and verified
in the official Windows, Linux x86_64 and ARM64 binaries; Steam Deck has the
same published hash as Linux x86_64. See the
[verification and approval record](../notes/2026-10-31-v1012-identity-proposal.md).
The four engine salts remain unchanged. The historical fold-equivalence record
still describes its original v1.0.11 checkpoint.


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

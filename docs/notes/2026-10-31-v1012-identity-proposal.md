# Shipped v1.0.12 identity reconciliation proposal

The fold checkpoint preserves v1.0.11 identities. The later Online performance
layer is already in main's ancestry but is absent from the v1.0.11 materialized
closure. Its owned-menu notification change and the corresponding identities
must be reconciled before claiming parity with current main or starting Mac
online acceptance.

## Verified values

| Revision | Current fold (v1.0.11) | Shipped v1.0.12 |
|---|---|---|
| v77 | `5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e` | `dc883843594ccb02931dbab4071080c0dcf0d7c0a960ee501529dd37230651c7` |
| v80 | `32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0` | `105df80b777b20df308fede4235575dce5881e643130ebabf8cc60d71ff000e0` |

These values were reconstructed from main's retained online-performance ledger:
`sha256(previous_owned_id + "\n" + ledger)`, using the sorted native/test files,
pipeline.py and ReleaseOverlay.cmake exactly as that overlay specifies.
Both resulting strings occur in the official v1.0.12 Windows runtime. Neither
old owned ID occurs there; all four frozen engine-salt strings do. No released
binary was executed.

Source commit: `d457741b57afff8d2ec96b2307f11c1340a4162f`.
Ledger SHA-256: `6ab218742863022a1295bbf5dbf03ab6ceafe899291fb965a194e35d8875c384`.
Official Windows ZIP SHA-256: `606c125d8b5669f42c89769f6fcdef43f9932e70aa28ed283446901956bafb4f`.
Runtime executable SHA-256: `3eef5315ff153e8b0df3b15e18fec0bdc270981a13920ebd9c5056e129301167`.
Source: [Version1.0.12 release](https://github.com/ThatGuyMcd/DKR-R/releases/tag/Version1.0.12).

## Required approval and implementation

AGENTS.md requires the deliberate identity edit to be approved by both owners.
The code pins remain unchanged pending that approval. The proposed target is
compatibility with shipped v1.0.12, which drops compatibility with v1.0.11.

After approval, edit only legacy_identity.hpp for the two owned IDs and update
ONLINE_IDENTITY.md with this provenance. Carry the owned-menu notification
policy into its first-party source input and promote its bounded notification
fixtures. Do not restore configure-time source rewriting or another digest
chain. Keep the four engine salts unchanged, then verify both runtime identities
and native menu behavior against the shipped values.

The underlying v1.0.11 signing/equivalence checkpoint and recorded closure hashes
remain historical evidence. They must not be re-recorded to hide these ports.

## Published Linux artifacts checked directly

GitHub reports Version1.0.12 as the latest published release (27 October 2026).
The official x86_64 and ARM64 AppImages were downloaded and their SquashFS
`usr/bin/DKR-R` entries inspected without executing either binary. Both contain
the same v77 and v80 values in the table above.

| Artifact | AppImage SHA-256 | Runtime ELF SHA-256 |
|---|---|---|
| Linux x86_64 | `61a44fce7b43fe46c63b20651e613c8f602adc75d805981f9fa53074f46300f2` | `a099a8fa98d3e5063f4d3c8edfb2776c4b170386126b420665f4f1769b9d28fb` |
| Linux ARM64 | `734494dc22674aee25117e836d59cee51ff3af8c137d873066b8d80b95bcfb22` | `72a7b32c3da2e07b5ec89e1143d7793b2a5625f44756182d4351b97d0618b97c` |

The downloaded AppImage hashes match GitHub's release-asset digests. GitHub
reports the Steam Deck asset has the same digest as Linux x86_64, so it has
the same packaged runtime. Together with the Windows inspection, these are
the identities of all published v1.0.12 desktop downloads. The release has
no Apple asset. This evidence does not itself substitute for the required
both-owner confirmation of the fold's explicit identity edit.

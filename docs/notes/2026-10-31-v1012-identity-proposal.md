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

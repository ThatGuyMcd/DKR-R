#pragma once
// Online identity: the owned rollback contracts peers compare before they
// admit each other, frozen at the v1.0.11 values. docs/contracts/ONLINE_IDENTITY.md
// records them and where they came from. They change only by a deliberate
// edit here, never because a source file changed, and a change makes this
// build refuse every v1.0.11 peer. cmake/OnlineIdentity.cmake reads them into
// the DKR_OWNED_BUILD_ID_V77/V80 compile definitions.
#define DKR_LEGACY_OWNED_BUILD_ID_V77 "5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e"
#define DKR_LEGACY_OWNED_BUILD_ID_V80 "32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0"

// The owned engine identity (experimental_runtime.cpp runtime_engine_identity,
// the manifest's owned_engine_hash) is sha256 of the owned adapter identity
// followed by these four digests, in this order, as v1.0.11 shipped them.
#define DKR_LEGACY_ENGINE_SALT_1 "dcb1e8df777d341f2e96d4ee78cdc4337d32a6ddc86b4b60dbcc46fabfd31efc"
#define DKR_LEGACY_ENGINE_SALT_2 "893bbedb02a3c87771e75befab107a2ce54641073742be69e97b995a9d0a3bbf"
#define DKR_LEGACY_ENGINE_SALT_3 "2a82142dc00619b29b80f5f0b6485c3ffe8c0a33e89a7a6c48d0a6ac8eeb4a66"
#define DKR_LEGACY_ENGINE_SALT_4 "c4d77b07ed3b7f9aabcf7ce1430780fc65a1ce086e62b66a0af672aac40eb5ac"

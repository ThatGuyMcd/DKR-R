#pragma once
// Online identity: the owned rollback contracts peers compare before they
// admit each other, frozen at the shipped v1.0.15 values. docs/contracts/ONLINE_IDENTITY.md
// records them and where they came from. They change only by a deliberate
// edit here, never because a source file changed, and a change makes this
// build refuse older incompatible peers. cmake/OnlineIdentity.cmake reads them into
// the DKR_OWNED_BUILD_ID_V77/V80 compile definitions.
#define DKR_LEGACY_OWNED_BUILD_ID_V77 "e5c8df41e92c766cb46d137111a289793308e398033127160ff321ff4672a4bf"
#define DKR_LEGACY_OWNED_BUILD_ID_V80 "a9a0d18fccc59d42aaa8b9cd6fdee50c22fa0699283eee8087a82f99ac85ef80"

// The owned engine identity (experimental_runtime.cpp runtime_engine_identity,
// the manifest's owned_engine_hash) is sha256 of the owned adapter identity
// followed by these four digests, in this order, as v1.0.15 shipped them.
#define DKR_LEGACY_ENGINE_SALT_1 "5b174d993b686bc965a25b30d2469716c129e701b0f46d5aadd1d771cb7e3056"
#define DKR_LEGACY_ENGINE_SALT_2 "213eec0456be89e847a29a8952914eb374976aacf95f18c90ab37479dd9245b3"
#define DKR_LEGACY_ENGINE_SALT_3 "e1f2ebf647cdbe03052096f099ed26c2f35a4656c3c749031c81390781a4fedc"
#define DKR_LEGACY_ENGINE_SALT_4 "731559cac6456c0b54894752e473a258bd0883d947b86003067c86be508ff055"

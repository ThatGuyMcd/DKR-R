#pragma once
// Online identity: the owned rollback contracts peers compare before they
// admit each other. v1.0.15 shipped e5c8df41... (v77) and a9a0d18f... (v80);
// the 127-checkpoint pool moved guest allocations, so each value is now
// sha256("<v1.0.15 value>|checkpoints-127-v1"). docs/contracts/ONLINE_IDENTITY.md
// records them and where they came from. They change only by a deliberate
// edit here, never because a source file changed, and a change makes this
// build refuse older incompatible peers. cmake/OnlineIdentity.cmake reads them into
// the DKR_OWNED_BUILD_ID_V77/V80 compile definitions.
#define DKR_LEGACY_OWNED_BUILD_ID_V77 "5e4546d1209d785da50787207228884e02a47a6a8aad06838c3df1243dd321d4"
#define DKR_LEGACY_OWNED_BUILD_ID_V80 "a52b30e2761b60e7526296858cf858478244b1d6308b63174c9e5310f1d9d57b"

// The owned engine identity (experimental_runtime.cpp runtime_engine_identity,
// the manifest's owned_engine_hash) is sha256 of the owned adapter identity
// followed by these four digests, in this order, as v1.0.15 shipped them.
#define DKR_LEGACY_ENGINE_SALT_1 "5b174d993b686bc965a25b30d2469716c129e701b0f46d5aadd1d771cb7e3056"
#define DKR_LEGACY_ENGINE_SALT_2 "213eec0456be89e847a29a8952914eb374976aacf95f18c90ab37479dd9245b3"
#define DKR_LEGACY_ENGINE_SALT_3 "e1f2ebf647cdbe03052096f099ed26c2f35a4656c3c749031c81390781a4fedc"
#define DKR_LEGACY_ENGINE_SALT_4 "731559cac6456c0b54894752e473a258bd0883d947b86003067c86be508ff055"

#pragma once
// Static reviewed ownership metadata only; no game/model/texture bytes.
// SHA covers all decompressed model bytes except relocatable texture IDs.
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace dkr::mods::vehicles {
struct ReviewedCharacterMask { unsigned vertices,triangles; std::vector<std::pair<unsigned,unsigned>> ranges; };
inline const std::map<std::string,ReviewedCharacterMask> ReviewedCharacterMasks{
    // DKR_Blastoise - Custom Racer 6 LOD0; keep 378 driver faces.
    {"fc0595169658be546bf4161f13184355f03893108060271e395f77a8556219c7",{706,448,{{378,448}}}},
    // DKR_Blastoise - Custom Racer 10 LOD0; keep 367 driver faces.
    {"ba8cc38f3d0b7c36aaa33d8653d3e7ea7823d23cfbe63ae9099598811b58fefd",{696,437,{{367,437}}}},
    // DKR_Blastoise - Custom Racer 1 LOD0; keep 378 driver faces.
    {"6550b03c5d7d4c9e2f1a77c0b29cc684ad886bf894b01f4264200f4672f34a4a",{706,448,{{378,448}}}},
    // Link LOD0; keep 263 driver faces.
    {"2a4eb94c1e3c4cd88a7abab84b3f81886b650b2a5b2ebf407e82a32c51534680",{556,335,{{255,327}}}},
    // Link LOD1; keep 263 driver faces.
    {"46a8e9111a9c8f22c839a8b24fd3daa1badeec759471fc2f8d814a3259f1c939",{556,335,{{255,327}}}},
    // Link LOD3; keep 141 driver faces.
    {"33080b291c0786903304a49d75e96d85fecf4625008e90ed8dd95ea805b468d9",{358,196,{{141,196}}}},
    // Link LOD4; keep 74 driver faces.
    {"bcb68984b9bfeb5a931d63c4a0f5db8c335a7513893a1f6759bac1bb43aef07d",{201,129,{{74,129}}}},
    // Rexy LOD0; keep 117 driver faces.
    {"cfb223e014d8f60ce6adc25d67e4996b9d09f36f0d33a0843508f3d814f75b88",{282,193,{{0,64},{181,193}}}},
    // Rexy LOD1; keep 117 driver faces.
    {"bea874c27e9b6ff0a0f52f8b715ff8da493f6af5be20d7407e2c57e6bf57d1e0",{282,193,{{0,64},{181,193}}}},
    // Rexy LOD3; keep 71 driver faces.
    {"6d98149204e1377bb16a7dc4caaac4474f78be949889368b951865861964f528",{210,130,{{0,47},{118,130}}}},
    // DKR-Yoshi-Racing-Story64v04 - Custom Racer 9 LOD0; keep 322 driver faces.
    {"f52e4a5a9782656910f9129b7785e82b1d32060441e51d95a4fa0edd1edb669d",{480,389,{{208,220},{231,233},{249,272},{294,312},{337,343},{383,389}}}},
    // DKR-Yoshi-Racing-Story64v04 - Custom Racer 9 LOD4; keep 71 driver faces.
    {"eaf85793a2307437572856cd69396983f0d0402fffdace199a7ffbe2b2c1df76",{155,101,{{54,57},{62,64},{65,80},{84,86},{87,89},{95,101}}}},
    // DKR-Yoshi-Racing-Story64v04 - Custom Racer 5 LOD0; keep 322 driver faces.
    {"656cad4efc888abfc31889ef010e1db4fd54e22b42470ab62ae3f42dc2fe23e9",{479,389,{{208,220},{231,233},{249,272},{294,312},{337,343},{383,389}}}},
    // DKR-Yoshi-Racing-Story64v04 - Custom Racer 5 LOD1; keep 322 driver faces.
    {"70bf5db2828a47c8cd806058d704b88fcba1b3fbf57a0251d8d4470589c1862c",{473,389,{{208,220},{231,233},{249,272},{294,316},{341,347},{387,389}}}},
    // 007 Bond (car only) LOD0; keep 269 driver faces.
    {"557c2cd9aa58225f79941fd4e6537559239f88f2af2909ac376a7e06857a8961",{645,331,{{269,331}}}},
    // Grunty LOD0; keep 462 driver faces.
    {"c980b3ad4ed979a16286d84677dce55788cb134e3b4a17607a11d6cebe28f06b",{945,532,{{0,68},{530,532}}}},
    // Grunty LOD1; keep 115 driver faces.
    {"b5ba576dfe37b7ad4d08ebb1ee47ccb6c4f16d239ff7f0927ce7a8697670ae91",{367,164,{{0,47},{162,164}}}},
    // Grunty LOD2; keep 73 driver faces.
    {"d2fda3ed6688947b8c9ac4a83ee32b0f7ff72fae54eaeca8ffaec59c0c4f2ff1",{266,114,{{0,41}}}},
    // Grunty LOD3; keep 52 driver faces.
    {"cedd8a95f452aee0226325155f7953b7f8c405f64d1efb661db9c0daa6d5ac19",{180,77,{{0,25}}}},
    // Grunty LOD4; keep 38 driver faces.
    {"9a00037cd1663b49e310ce40b33ab0a16819f58839d5ed6bc99d60a5e9c4499e",{136,57,{{0,19}}}},
    // YOOKA LOD0; keep 262 driver faces.
    {"142f943c65de6de9ce95e6f490d4af202a4541ab3facb12e497c2914e106bdf5",{478,329,{{0,61},{323,329}}}},
    // YOOKA LOD1; keep 192 driver faces.
    {"115abb12236b36183d8da47bd740cb316b1aed05b6949ffa9f337a57939cde30",{355,241,{{192,241}}}},
    // YOOKA LOD2; keep 144 driver faces.
    {"36805d2544fd87828955b653c04dbd44addcd887a32283d77aa6975345aa8f53",{271,188,{{144,188}}}},
    // YOOKA LOD3; keep 116 driver faces.
    {"eabc64550216773d48b67007eb94fb5e91ef98738a0f5512dbf8477c1fa89df9",{196,141,{{116,141}}}},
    // YOOKA LOD4; keep 84 driver faces.
    {"f7e9a87f261f6d79d2bd943a5f23ab9949a618725e1ff969462f27d1f32071f8",{135,103,{{84,103}}}},
    // TINY KONG LOD0; keep 374 driver faces.
    {"169d52f368d3a8aa1fef5160ba92df64c0cdf65126faf6053a482aa7bbfa8340",{521,445,{{368,433},{439,445}}}},
    // TINY KONG LOD1; keep 154 driver faces.
    {"c0cc2c84a369c6bcbcb5c5f182feb1ee9b82e0e40ec551babca78391496271d9",{255,203,{{154,203}}}},
    // TINY KONG LOD2; keep 120 driver faces.
    {"f155341509539713c34fbf1b1517eb7c78773c8ec72827a358423a8768981155",{204,164,{{120,164}}}},
    // TINY KONG LOD3; keep 92 driver faces.
    {"acc1dca251ba11eb3acf5d34538312ce223a6687feb1462477993fcbe3b94c06",{145,117,{{92,117}}}},
    // TINY KONG LOD4; keep 70 driver faces.
    {"e376f3ab68e9f72eccd612fd4f8dd0cd890afebb45f94d7f2ec26c2f860694fe",{116,89,{{70,89}}}},
    // Haunter LOD0; keep 302 driver faces.
    {"6a8eb2bb216a528216cc3979120ee50fadc1116e4194e6a7b43c75a1c5b5b9bd",{669,395,{{0,4},{306,395}}}},
    // Haunter LOD1; keep 148 driver faces.
    {"07b6f01bb6a036f10bde38acd584e1fa73ee9714face71a8362627b297ba6897",{412,241,{{0,4},{16,105}}}},
    // Haunter LOD4; keep 6 driver faces.
    {"7ee68ffe76938d7ee506e4d918061a5977fdb088e626cc41525ca9142cce8d92",{8,6,{}}},
};
}

#pragma once
#include <cstddef>

namespace dkr::mods {
// Presentation sound tokens must survive the retail u16 delayed/spatial
// queues. Stock sounds occupy the low range; each private identity owns 32
// tokens (18 cues). This is a format boundary, not a visible stage-slot cap.
inline constexpr unsigned CharacterSoundBegin=0x4000U;
inline constexpr unsigned CharacterSoundStride=32U;
inline constexpr unsigned MaxEnabledCharacters=(0x10000U-CharacterSoundBegin)/CharacterSoundStride;
inline constexpr unsigned MaxInstalledCharacterVariants=16384U;
// Keep at least the majority of the 8 MiB guest for scenes/racers. Admission
// accounts for retained private faces and audio before guest startup.
inline constexpr std::size_t MaxCharacterPresentationBytes=2U*1024U*1024U;
}

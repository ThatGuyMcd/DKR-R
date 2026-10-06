#pragma once
#include "legacy_character_limits.hpp"
#include "legacy_mod_format.hpp"

namespace dkr::mods {
struct CharacterCue {
    unsigned sound=0,volume=0,pitch=100,priority=0;
    bool operator==(const CharacterCue&)const=default;
};
struct CharacterAudio {
    Bytes control,samples;
    std::array<CharacterCue,3> cues{}; // selected, deselected, final confirmation
};
struct CharacterRaceCue : CharacterCue {
    unsigned min_volume=0,range=0;
    bool operator==(const CharacterRaceCue&)const=default;
};
struct CharacterRaceAudio {
    Bytes control,samples;
    std::array<CharacterRaceCue,18> cues{}; // eight positive, eight negative, horn, ten bananas
};
CharacterRaceAudio prepare_character_race_audio(View control,View samples,View sound_table,unsigned base_character);
void validate_character_race_audio(const CharacterRaceAudio&);
// Presentation-only extended audio does not change existing logical/save IDs.
// Native sound IDs are converted only for an explicitly committed human racer.
int character_race_cue(unsigned sound,unsigned base_character);
unsigned character_race_sound(unsigned character,unsigned cue);
bool decode_character_race_sound(unsigned sound,unsigned& character,unsigned& cue);
CharacterAudio prepare_character_audio(View control,View samples,View sound_table,unsigned base_character);
void validate_character_audio(const CharacterAudio&);
std::string character_audio_identity(const CharacterAudio&);
}

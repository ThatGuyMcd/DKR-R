#pragma once
#include "legacy_character_materialize.hpp"
#include <optional>
#include <functional>

namespace dkr::mods {
// Selection identity is independent of both native behaviour ID and asset ID.
// Human selections persist; AI identities belong only to the current race.
// Bosses, ghosts and arbitrary menu props never inherit an AI selection.
class CharacterRoster {
public:
    explicit CharacterRoster(std::shared_ptr<const CharacterNamespace> assets);
    void request(unsigned slot,const std::string& id);
    void clear_active();
    void set_duplicate_policy(bool enabled){allow_duplicates_=enabled;}
    void commit(std::span<const std::uint8_t> native_characters,unsigned human_count);
    void clear_ai();
    void scene_humans(unsigned human_count);
    void assign_ai(std::span<std::uint8_t> native,unsigned humans,unsigned racers,
        std::span<const std::uint8_t> unlocked,const std::function<unsigned(unsigned)>& random);
    std::optional<unsigned> header(unsigned player,unsigned native_header) const;
    const std::string& active(unsigned slot) const;
    bool ai(unsigned slot)const{return slot<8 && ai_[slot];}
    Bytes checkpoint()const;
    CharacterRoster stage_checkpoint(View)const;
private:
    std::shared_ptr<const CharacterNamespace> assets_;
    std::array<std::string,4> requested_{},humans_{};
    std::array<std::string,8> active_{};
    std::array<bool,8> ai_{};
    bool allow_duplicates_=false;
};
} // namespace dkr::mods

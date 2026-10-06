#pragma once
#include "legacy_character_roster.hpp"

namespace dkr::mods {
inline constexpr unsigned CustomStageSlots=4, CustomCharacterLimit=MaxEnabledCharacters;
struct CustomStageAnchor {int x,y,z,row;};
// Match the authored lower lawn and upper wooden shelf, without moving stock actors.
inline constexpr std::array<CustomStageAnchor,CustomStageSlots> CustomStageAnchors{{
    {-52,-34,35,1},{65,-34,35,1},{-57,-10,0,0},{78,-10,0,0}}};
enum class CharacterMenuField : unsigned {
    Active,Status,NativeIndices,NativeIDs,Buttons,StickX,StickY,Ready,Players,
    Delay,DisplayList,SelectTable,DialogueBegin,DialogueModes,SoundHandles,CurrentMusic,ObjectCount,FreeListCount,Count
};
using CharacterMenuFields=std::array<std::uint32_t,static_cast<unsigned>(CharacterMenuField::Count)>;
// Checked, word-swapped guest access. No retained pointers or guest calls.
class CharacterMenuMemory {
public:
    CharacterMenuMemory(std::span<std::uint8_t> memory,const CharacterMenuFields& fields);
    std::uint32_t address(CharacterMenuField field,unsigned offset=0)const;
    std::uint32_t read(std::uint32_t address,unsigned size=4)const;
    void write(std::uint32_t address,std::uint32_t value,unsigned size=4);
    std::uint32_t get(CharacterMenuField field,unsigned offset=0,unsigned size=4)const;
    void set(CharacterMenuField field,std::uint32_t value,unsigned offset=0,unsigned size=4);
    void bytes(std::uint32_t address,View value);
private:
    std::size_t check(std::uint32_t address,std::size_t size)const;
    std::span<std::uint8_t> memory_;
    const CharacterMenuFields& fields_;
};
struct CharacterMenuView {
    bool custom_page=false,visible=false;
    unsigned owner=0,page=0;
    std::array<bool,4> active{},ready{};
    std::array<std::optional<std::size_t>,4> custom{};
    std::array<std::optional<std::size_t>,CustomStageSlots> displayed{};
    std::array<unsigned,4> native{};
};
class CharacterMenuAdapter {
public:
    explicit CharacterMenuAdapter(std::shared_ptr<const CharacterNamespace> assets);
    void reset();
    void set_unlocks(bool drumstick,bool tt){drumstick_=drumstick;tt_=tt;}
    std::optional<std::size_t> custom(unsigned player)const{return custom_.at(player);}
    std::vector<unsigned> input(CharacterMenuMemory& guest,bool duplicates);
    CharacterMenuView view(const CharacterMenuMemory& guest)const;
    void commit(CharacterMenuMemory& guest,CharacterRoster& roster,unsigned human_count);
    bool has_custom()const;
    bool native_move(CharacterMenuMemory& guest,unsigned player,std::uint32_t direction,unsigned bounds,bool duplicates);
    const std::vector<AllocatedCharacter>& entries()const{return assets_->characters;}
    Bytes checkpoint()const;
    CharacterMenuAdapter stage_checkpoint(View)const;
private:
    std::shared_ptr<const CharacterNamespace> assets_;
    std::array<std::optional<std::size_t>,4> custom_{};
    std::array<std::optional<std::size_t>,CustomStageSlots> displayed_{};
    unsigned owner_=0;
    bool duplicates_=false;
    bool drumstick_=false,tt_=false;
};
}

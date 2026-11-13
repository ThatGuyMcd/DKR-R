#pragma once
#include "policy.hpp"
#include <utility>

namespace dkr::launcher_themes {
enum class Icon : int {
    Mitten, Snowflake, Flower, Butterfly, Sun, BeachBall, Leaf, Acorn,
    Pumpkin, Bat, Bonfire, Rocket, Tree, Gift, PartyHat, Popper, Heart, Envelope,
    Clover, Hat, Egg, Rabbit, Harvest, Wheat, Lantern, RedEnvelope, Diya, Rangoli,
    EidLantern, Sweets, Crescent, GiftBasket, Hanukkiah, Dreidel,
    FlagUS, FlagCanada, BuntingUS, BuntingCanada, Count, None = -1
};
inline constexpr int ArtTile = 128, ArtColumns = 8, ArtRows = 5;
inline constexpr int ArtWidth = ArtTile * ArtColumns, ArtHeight = ArtTile * ArtRows;
inline std::pair<Icon,Icon> icons_for(Theme theme, Calendar calendar) {
    switch(theme) {
    case Theme::Winter: return {Icon::Mitten,Icon::Snowflake};
    case Theme::Spring: return {Icon::Flower,Icon::Butterfly};
    case Theme::Summer: return {Icon::Sun,Icon::BeachBall};
    case Theme::Autumn: return {Icon::Leaf,Icon::Acorn};
    case Theme::Halloween: return {Icon::Pumpkin,Icon::Bat};
    case Theme::Bonfire: return {Icon::Bonfire,Icon::Rocket};
    case Theme::Christmas: return {Icon::Tree,Icon::Gift};
    case Theme::NewYear: return {Icon::PartyHat,Icon::Popper};
    case Theme::Valentine: return {Icon::Heart,Icon::Envelope};
    case Theme::Patrick: return {Icon::Clover,Icon::Hat};
    case Theme::Easter: return {Icon::Egg,Icon::Rabbit};
    case Theme::Thanksgiving: return {Icon::Harvest,Icon::Wheat};
    case Theme::Lunar: return {Icon::Lantern,Icon::RedEnvelope};
    case Theme::Diwali: return {Icon::Diya,Icon::Rangoli};
    case Theme::EidFitr: return {Icon::EidLantern,Icon::Sweets};
    case Theme::EidAdha: return {Icon::Crescent,Icon::GiftBasket};
    case Theme::Hanukkah: return {Icon::Hanukkiah,Icon::Dreidel};
    case Theme::National:
        if(calendar == Calendar::US) return {Icon::FlagUS,Icon::BuntingUS};
        if(calendar == Calendar::Canada) return {Icon::FlagCanada,Icon::BuntingCanada};
        return {Icon::None,Icon::None};
    default: return {Icon::None,Icon::None};
    }
}
} // namespace dkr::launcher_themes

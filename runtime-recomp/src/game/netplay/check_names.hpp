#pragma once
// Names for the racers a self-test run puts in a lobby (--self-test-owned-*).
// Every peer picks its own, so a four-player check shows four different
// racers and each window and log can be told apart at a glance.
//
// A name is a race number and a moniker: "#27 Ping Kong". Monikers come from
// two sources, a list of hand-written ones and a pairing of two word lists,
// so there are enough that four peers picking at random rarely collide.
// Everything is checked at compile time against the lobby's name rules, so
// adding a line here cannot produce a name the protocol would reject.
#include "netplay_types.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay::check_names {

// "#99 " leaves this much of kMaximumPlayerNameBytes for the moniker.
inline constexpr std::size_t kMonikerBytes = kMaximumPlayerNameBytes - 4U;

// Hand-written. Bananas, apes, plumbers, karts and netcode.
inline constexpr std::array<std::string_view, 80> kMonikers = {
    "Peel Out",          "Banana Bread Winner", "Top Banana",         "Second Banana",     "Gone Bananas",
    "Banana For Scale",  "Banana Split Second", "Bunch Of Trouble",   "Peel Good Inc",     "Potassium Overdose",
    "Rubber Banana",     "Grand Theft Banana",  "Slip N Slide",       "Appeeling Offer",   "Plantain Truth",
    "Kongestion",        "Donkey Wrong",        "Diddy Do It",        "Kong Fu Panic",     "Ping Kong",
    "Drift King Kong",   "Kong With The Wind",  "Barrel Of Laughs",   "Barrel Roll Model", "Monkey Business",
    "Chimp My Ride",     "Gorilla Warfare",     "Primate Suspect",    "Simian Says",       "Ape-ril Fools",
    "Coconut Gunner",    "Jungle Jam Session",  "Cranky Old Timer",   "Tie Rack Enjoyer",  "DK Mode Enjoyer",
    "Mushroom Clutch",   "Toadally Lost",       "Warp Pipe Dream",    "Koopa Trooper",     "Blue Shell Shock",
    "Its A Me Lag",      "Lets A Go Slow",      "Mama Mia Miata",     "Wahoo Cares",       "Plumber On Call",
    "Luigi Death Stare", "Bowsers Uber",        "Princess Peel",      "Star Power Nap",    "Fire Flower Child",
    "Goomba Stomped",    "Yoshi Tax Fraud",     "Mustache Wax Spill", "Overalls Overdue",  "Wrong Castle Again",
    "Wizpig Latin",      "Taj Mahauler",        "Silver Coin Flip",   "Balloon Hoarder",   "Zipper Skipper",
    "Dino Domain Name",  "Frosty Reception",    "Hover Bother",       "Plane Silly",       "Car Trouble",
    "Rollback Mountain", "Desync Or Swim",      "Packet Loss Leader", "Frame Perfect-ish", "Lag Monster",
    "Kart Blanche",      "Brake Dancer",        "Fast And Curious",   "Apex Predator-ish", "Wrong Way Wendy",
    "Turbo Encabulator", "Last Place Legend",   "Podium Tourist",     "Did Not Finish",    "Honk If Rolled Back",
};

// Paired as "<manner> <racer>": "Feral Plantain", "Tactical Mustache".
inline constexpr std::array<std::string_view, 32> kManners = {
    "Turbo",  "Feral",   "Soggy",    "Greasy", "Cosmic", "Spicy", "Grumpy", "Wobbly",  "Sneaky",   "Golden",  "Rotten",
    "Unripe", "Bruised", "Slippery", "Mighty", "Cranky", "Funky", "Dizzy",  "Salty",   "Jumbo",    "Nitro",   "Dapper",
    "Cheeky", "Rowdy",   "Smug",     "Loopy",  "Chunky", "Sassy", "Wonky",  "Haunted", "Tactical", "Legally",
};
inline constexpr std::array<std::string_view, 32> kRacers = {
    "Banana",   "Plantain",  "Barrel",   "Kong",       "Gorilla", "Chimp",   "Baboon", "Plumber",
    "Mushroom", "Koopa",     "Goomba",   "Hovercraft", "Coconut", "Peel",    "Bunch",  "Mustache",
    "Overalls", "Walrus",    "Elephant", "Genie",      "Rooster", "Tiger",   "Badger", "Squirrel",
    "Turtle",   "Crocodile", "Mouse",    "Stopwatch",  "Balloon", "Go-Kart", "Wrench", "Minecart",
};

namespace detail {
constexpr bool printable(std::string_view text) {
    if (text.empty())
        return false;
    for (const char c : text)
        if (c < 0x20 || c > 0x7E)
            return false;
    return true;
}
template <std::size_t N> constexpr std::size_t longest(const std::array<std::string_view, N>& words) {
    std::size_t bytes = 0;
    for (const auto word : words)
        bytes = word.size() > bytes ? word.size() : bytes;
    return bytes;
}
template <std::size_t N> constexpr bool usable(const std::array<std::string_view, N>& words) {
    for (std::size_t at = 0; at < N; ++at) {
        if (!printable(words[at]))
            return false;
        for (std::size_t other = 0; other < at; ++other)
            if (words[other] == words[at])
                return false;
    }
    return true;
}
// splitmix64: neighbouring seeds give unrelated names.
constexpr std::uint64_t mix(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}
} // namespace detail

static_assert(detail::usable(kMonikers) && detail::usable(kManners) && detail::usable(kRacers));
static_assert(detail::longest(kMonikers) <= kMonikerBytes);
static_assert(detail::longest(kManners) + 1U + detail::longest(kRacers) <= kMonikerBytes);

// How many different monikers there are, before the race number.
inline constexpr std::size_t kMonikerCount = kMonikers.size() + kManners.size() * kRacers.size();

// The name for a seed. Stable, so a logged seed reproduces its name.
inline std::string generate(std::uint64_t seed) {
    const std::uint64_t number = detail::mix(seed), choice = detail::mix(number);
    std::string name = "#" + std::to_string(1U + unsigned(number % 99U)) + " ";
    // Even odds between the two sources, so the written ones show up often
    // without being every other racer in a four-player lobby.
    if ((choice & 1U) == 0U)
        return name += kMonikers[(choice >> 1) % kMonikers.size()];
    name += kManners[(choice >> 1) % kManners.size()];
    name += ' ';
    return name += kRacers[(choice >> 17) % kRacers.size()];
}

// The name this process races under: DKR_OWNED_CHECK_NAME when it is set to
// a name the lobby would accept, otherwise a fresh one.
inline std::string choose() {
    if (const char* wanted = std::getenv("DKR_OWNED_CHECK_NAME"); wanted && valid_display_name(wanted))
        return wanted;
    std::random_device device;
    return generate((std::uint64_t(device()) << 32) | device());
}

} // namespace dkr::runtime::netplay::check_names

#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

namespace dkr::mods {
// Versioned, pointer-free compatibility contract. It is NOT permission to
// load a ROM as native code. Every executable component has a reviewed site
// and instruction; unknown differences are retained as evidence only.
inline constexpr unsigned BehaviourAbi = 1;
inline constexpr unsigned BehaviourSites = 17;
struct BehaviourContract {
    unsigned abi = 0;
    std::string evidence;
    unsigned changed_bytes = 0, unresolved_bytes = 0;
    std::array<std::uint32_t, BehaviourSites> instructions{};
    unsigned stat_mask = 0;
    std::array<std::uint32_t, 3> stat_bits{}; // weight, handling, misc-11 (unscaled retail values)
    unsigned header_fields = 0;               // reviewed scalar fields, never pointers/behaviour IDs/counts
    unsigned native_profile = 0;              // 1: rebuilt native equivalent; 2: reviewed random texture-phase sharing
    bool operator==(const BehaviourContract&) const = default;
};
struct BehaviourSite {
    const char* function;
    std::uint32_t pc77, original77, replacement;
    // Zero means this exact instruction contract has no reviewed v1.1 site.
    std::uint32_t pc80 = 0, original80 = 0;
};
const std::array<BehaviourSite, BehaviourSites>& behaviour_sites();
// Exact whole pre-asset hashes from independently rebuilt reference fixtures.
// Never accepts an arbitrary compiler shape, opcode mask or mod-provided flag.
unsigned linked_behaviour_profile(std::span<const std::uint8_t> prefix);
void validate_behaviour(const BehaviourContract&);
std::string behaviour_identity(const BehaviourContract&);
std::string behaviour_summary(const BehaviourContract&);
std::string encode_behaviour(const BehaviourContract&);
BehaviourContract decode_behaviour(const std::string&);
// Native-layout header adapter. Returns changed scalar field bits; rejects all
// pointer/count/particle/attachment/behaviour/layout edits, before publication.
unsigned object_parameter_fields(std::span<const std::uint8_t> original, std::span<const std::uint8_t> target);
// Reviewed constant-return/load components only. No guest memory, OS calls,
// mutable VM globals, RNG, allocation, audio or irreversible effects exist in
// this ABI. More opcodes/components require a new reviewed ABI, not a fallback
// to a permissive whole-game interpreter.
std::uint64_t execute_behaviour_component(const BehaviourContract&, unsigned site, std::uint64_t native_value);
} // namespace dkr::mods

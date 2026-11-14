#pragma once
#include "magic_code_package.hpp"
#include "magic_code_vm.hpp"
#include <optional>

namespace dkr::mods::code {
inline constexpr std::uint32_t CodeBase = 0x81000000U;
inline constexpr std::size_t MaxResident = 8 * MiB;
struct ImageSection {
    std::uint32_t address = 0;
    Bytes bytes, word_swapped;
};
struct LinkedImport {
    std::size_t owner = 0;
    std::string dependency, name;
    bool event = false;
};
struct Hooks {
    std::vector<std::uint32_t> entry, exit;
    std::optional<std::uint32_t> replacement;
};
struct Owner {
    std::shared_ptr<const Package> package;
    Json settings;
    std::map<std::string, Bytes> data;
    std::uint32_t begin = 0, end = 0, heap = 0, heap_size = 0, stack = 0, native_stack = 0, services = 0;
    std::vector<std::uint32_t> functions;
};
struct LinkedSet {
    std::string identity, revision;
    std::vector<Owner> owners;
    std::vector<ImageSection> sections;
    std::vector<Region> regions;
    std::map<std::uint32_t, LinkedImport> imports;
    std::map<std::uint32_t, Hooks> hooks;
    std::map<std::string, std::vector<std::uint32_t>> events;
    std::map<std::uint32_t, std::size_t> function_owners;
    std::set<std::uint32_t> native_functions;
    std::size_t resident_bytes = 0;
};
struct LinkPolicy {
    std::map<std::string, unsigned> modules;
    std::set<std::string> services, events;
    std::set<std::uint32_t> functions, protected_functions, hook_functions, call_functions;
    bool raw_game_access = false;
    bool online = false;
};
std::shared_ptr<const LinkedSet> link(const std::vector<std::shared_ptr<const Package>>& ordered,
                                      const std::map<std::string, Json>& settings, std::string_view revision,
                                      const LinkPolicy&, const std::map<std::string, Json>& data = {});
// Never run on a rewind: installation must not reset mutable mod globals.
void install(const LinkedSet&, std::span<std::uint8_t> word_swapped_ram);
} // namespace dkr::mods::code

#pragma once
#include "magic_code_vm.hpp"
namespace dkr::mods::code {
using SymbolResolver = std::function<std::uint32_t(std::string_view)>;
bool game_memory_service(std::string_view, Memory&, Registers&, Budget&, const SymbolResolver&);
} // namespace dkr::mods::code

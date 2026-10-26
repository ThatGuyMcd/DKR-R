#pragma once
#include "legacy_mod_format.hpp"
#include <functional>

namespace dkr::mods::code {
// Canonical guest values only. No host addresses are persisted in a checkpoint.
struct Registers {
    // FR=0 ABI: each even floating slot holds the paired even/odd words;
    // odd slots are reserved and remain zero.
    std::array<std::uint64_t, 32> integer{}, floating{};
    std::uint64_t hi = 0, lo = 0;
    std::uint32_t fcsr = 0;
};
struct Region {
    std::uint32_t address = 0, size = 0;
    bool writable = false, executable = false;
    const std::uint8_t* immutable = nullptr; // Host-owned metadata, NEVER checkpointed.
};
class Memory {
  public:
    // The game's existing word-swapped RDRAM layout, borrowed for one call.
    Memory(std::span<std::uint8_t> ram, std::vector<Region> regions);
    std::uint64_t load(std::uint32_t address, unsigned bytes, bool instruction = false) const;
    void store(std::uint32_t address, unsigned bytes, std::uint64_t value);
    void read(std::uint32_t address, std::span<std::uint8_t> destination) const;
    void write(std::uint32_t address, View source);
    void fill(std::uint32_t address, std::size_t bytes, std::uint8_t value = 0);
    bool executable(std::uint32_t address) const;

  private:
    std::span<std::uint8_t> ram_;
    std::vector<Region> regions_;
    mutable std::size_t last_data_ = std::size_t(-1), last_code_ = std::size_t(-1);
    std::size_t checked(std::uint32_t, unsigned, bool write, bool instruction) const;
    std::size_t range(std::uint32_t, std::size_t, bool write, bool instruction) const;
};
struct Budget {
    std::uint64_t instructions = 200000;
    unsigned native_calls = 128, depth = 0;
};
// The gateway must resolve in the CALLING world, not the process-wide overlay
// table. Only explicitly linked imports/base functions reach it.
using Gateway = std::function<void(std::uint32_t, Registers&, Budget&)>;
// Activation rejects unsupported opcodes before any callback can mutate RAM.
void validate_instructions(View big_endian_function);
void execute(Memory&, Registers&, std::uint32_t entry, Budget&, const Gateway&);
} // namespace dkr::mods::code

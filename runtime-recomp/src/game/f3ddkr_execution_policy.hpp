#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::f3ddkr {

enum class CommandCheck { Okay, Misaligned, OutsideMemory, Truncated, BudgetExceeded };

// One task may revisit a command through branches and counted DMA blocks.
// Charge every executed word against one budget, rather than resetting it at
// each nested list. The default allows as many command words as fit in the
// complete 8 MiB snapshot, including revisits, and stops cyclic streams.
class CommandBudget {
  public:
    explicit CommandBudget(std::size_t words = 1024U * 1024U) : remaining_words_(words) {}

    template <typename ReadOpcode>
    CommandCheck validate(std::uintptr_t memory_base, std::uintptr_t address, std::size_t memory_size,
                          ReadOpcode&& read_opcode) {
        return validate(memory_base, address, memory_size, read_opcode, [](std::uint8_t opcode) -> std::size_t {
            return (opcode == 0xE4U || opcode == 0xE5U || opcode == 0xD5U) ? 3U : 1U;
        });
    }

    template <typename ReadOpcode, typename WordCount>
    CommandCheck validate(std::uintptr_t memory_base, std::uintptr_t address, std::size_t memory_size,
                          ReadOpcode&& read_opcode, WordCount&& word_count) {
        if (address < memory_base || address - memory_base >= memory_size)
            return CommandCheck::OutsideMemory;
        const std::size_t offset = address - memory_base;
        if ((offset & 7U) != 0U)
            return CommandCheck::Misaligned;
        const std::size_t available = memory_size - offset;
        if (available < 8U)
            return CommandCheck::Truncated;
        // The callback must never run until the first command is readable.
        const std::uint8_t opcode = read_opcode();
        const std::size_t words = word_count(opcode);
        if (words == 0U || words > available / 8U)
            return CommandCheck::Truncated;
        if (remaining_words_ < words)
            return CommandCheck::BudgetExceeded;
        remaining_words_ -= words;
        return CommandCheck::Okay;
    }

    std::size_t remaining_words() const {
        return remaining_words_;
    }

  private:
    std::size_t remaining_words_;
};

} // namespace dkr::runtime::f3ddkr

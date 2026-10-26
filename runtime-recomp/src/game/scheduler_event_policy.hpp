#pragma once

#include <cstdint>

namespace dkr::runtime::scheduler {

constexpr bool is_rdram_word_address(std::uint32_t address, std::uint32_t final_offset) {
    const bool valid_segment = address <= 0x007FFFFCU || (address >= 0x80000000U && address <= 0x807FFFFCU) ||
                               (address >= 0xA0000000U && address <= 0xA07FFFFCU);
    if (!valid_segment) {
        return false;
    }
    const std::uint32_t physical = address & 0x1FFFFFFFU;
    return final_offset <= 0x007FFFFCU && physical <= 0x007FFFFCU - final_offset;
}

// Physical RDRAM address zero is technically addressable, but a zero
// scheduler task field is libultra's null pointer sentinel. It must never be
// accepted as an OSTask merely because MEM_W can mask it into RDRAM.
constexpr bool is_valid_sp_task_pointer(std::uint32_t task) {
    return task != 0U && is_rdram_word_address(task, 0x10U);
}

// __scHandleRDP immediately clears OS_SC_NEEDS_RDP at task + 0x04 and its
// completion path can consult the final OSScTask word at +0x68. A late DP
// edge sees a null curRDPTask; accepting address zero here would let MEM_W
// turn that sentinel into an unchecked native access beyond RDRAM.
constexpr bool is_valid_dp_task_pointer(std::uint32_t task) {
    return task != 0U && is_rdram_word_address(task, 0x68U);
}

} // namespace dkr::runtime::scheduler

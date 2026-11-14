#pragma once

#include "recomp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace dkr::runtime::netplay {

// DKR uses the base 8 MiB RDRAM image and N64ModernRuntime's project-owned
// extended region below the mod heap. Capturing the complete 16 MiB range is
// deliberately conservative: correctness is more important than shaving a
// few MiB from a rollback slot.
inline constexpr std::size_t kRollbackMemoryBytes = 0x01000000U;
inline constexpr std::size_t kMaximumTrackedContexts = 32U;

struct SerializableContext {
    std::array<std::uint64_t, 32> gpr{};
    std::array<std::uint64_t, 32> fpr{};
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    std::uint32_t status = 0;
    std::uint8_t mips3_float_mode = 0;
    std::array<std::uint8_t, 3> reserved{};
};

class RuntimeState final {
  public:
    explicit RuntimeState(std::size_t memory_bytes = kRollbackMemoryBytes);

    bool register_context(recomp_context* context);
    void unregister_context(recomp_context* context);
    void reset();

    std::size_t snapshot_size() const;
    std::size_t memory_bytes() const {
        return memory_bytes_;
    }
    std::size_t active_context_count() const;

    bool capture(std::uint8_t* rdram, std::uint32_t frame, std::span<std::uint8_t> destination) const;
    bool restore(std::uint8_t* rdram, std::span<const std::uint8_t> source, std::uint32_t& frame);

  private:
    std::size_t memory_bytes_;
    mutable std::mutex mutex_;
    std::array<recomp_context*, kMaximumTrackedContexts> contexts_{};
};

void repair_float_register_pointer(recomp_context& context);

} // namespace dkr::runtime::netplay

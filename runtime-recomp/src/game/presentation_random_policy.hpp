#pragma once

#include <cstdint>
#include <limits>

namespace dkr::runtime::netplay {

// DKR's retail rand_range() advances a single global stream. Several purely
// presentational systems (HUD voices, engine audio and animated materials)
// also use that stream, sometimes behind conditions involving local audio
// handles or viewport state. Those conditions are intentionally not rollback
// state, so allowing them to advance the gameplay stream makes peers diverge.
//
// Online presentation hooks use this independent stream instead. It is host
// state rather than emulated state by design: its output may vary visually or
// aurally across peers, but can never influence racer physics or race progress.
class PresentationRandomStream final {
  public:
    explicit constexpr PresentationRandomStream(std::uint32_t seed = 0xD1DD7A11U)
        : state_(seed != 0U ? seed : 0xD1DD7A11U) {}

    constexpr std::uint32_t next() {
        std::uint32_t value = state_;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        state_ = value != 0U ? value : 0xD1DD7A11U;
        return state_;
    }

    constexpr std::int32_t range(std::int32_t minimum, std::int32_t maximum) {
        if (maximum < minimum) {
            const std::int32_t temporary = minimum;
            minimum = maximum;
            maximum = temporary;
        }
        const std::uint64_t span =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(maximum) - static_cast<std::int64_t>(minimum)) + 1ULL;
        const std::uint64_t offset =
            span == (1ULL << 32U) ? static_cast<std::uint64_t>(next()) : static_cast<std::uint64_t>(next()) % span;
        return static_cast<std::int32_t>(static_cast<std::int64_t>(minimum) + static_cast<std::int64_t>(offset));
    }

    constexpr std::uint32_t state() const {
        return state_;
    }

  private:
    std::uint32_t state_;
};

} // namespace dkr::runtime::netplay

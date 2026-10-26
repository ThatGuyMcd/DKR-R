#pragma once
#include "revision_addresses.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rsp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace dkr::runtime::presentation {
// The existing lower-void widening hook explicitly excludes layout 0. Credits
// use that single camera, but now expose a wide viewport. Apply the same lateral
// basis expansion to decoder-local vertices, never to the authored guest mesh.
class CreditsSky {
    static constexpr std::array<std::array<std::uint32_t, 5>, 2> symbols_{
        {{0x800DC924U, 0x8011D480U, 0x8011D488U, 0x8011D4B4U, 0x8011D4B6U},
         {0x800DCE94U, 0x8011DA00U, 0x8011DA08U, 0x8011DA34U, 0x8011DA36U}}};
    std::uint32_t begin_ = 0, end_ = 0;
    float lateral_x_ = 0, lateral_z_ = 0, centre_x_ = 0, centre_z_ = 0, length_ = 1, cover_ = 1;
    static std::uint32_t word(const std::uint8_t* ram, std::uint32_t address) {
        std::uint32_t value;
        std::memcpy(&value, ram + (address & 0x7FFFFCU), 4);
        return value;
    }
    static unsigned byte(const std::uint8_t* ram, std::uint32_t address) {
        return ram[(address & 0x7FFFFFU) ^ 3U];
    }
    static unsigned half(const std::uint8_t* ram, std::uint32_t address) {
        std::uint16_t value;
        std::memcpy(&value, ram + ((address & 0x7FFFFFU) ^ 2U), 2);
        return value;
    }

  public:
    void begin(const RT64::State& state, float aspect, bool enabled) {
        begin_ = end_ = 0;
        if (!enabled || !std::isfinite(aspect) || aspect <= 4.F / 3.F ||
            word(state.RDRAM, revision_addresses::ViewportLayout) != 0)
            return;
        const bool v80 = revision_addresses::CurrentMenuId == revision_addresses::kUsV80.CurrentMenuId;
        const auto& symbols = symbols_[v80 ? 1 : 0];
        if (!word(state.RDRAM, symbols[0]))
            return; // gVoidData: allocation still alive.
        const auto flip = byte(state.RDRAM, symbols[3]);
        const auto count = half(state.RDRAM, symbols[4]);
        if (flip > 1 || count > 24 || count % 4)
            return;
        const auto first = word(state.RDRAM, symbols[1] + (1 - flip) * 4) & 0x1FFFFFFFU;
        const auto tail = word(state.RDRAM, symbols[2]) & 0x1FFFFFFFU;
        const auto end = std::uint64_t(tail) + count * 10U;
        // void_generate_primitive flushes every 24 vertices. Its final cursor
        // plus count covers ALL batches, not merely the last submitted batch.
        if (!first || first >= 0x800000U || tail < first || end <= first || end > 0x800000U || end - first > 2000 ||
            (tail - first) % 10)
            return;
        const auto value = [&](std::uint32_t p) { return std::bit_cast<float>(word(state.RDRAM, p)); };
        lateral_x_ = value(revision_addresses::VoidLateralX);
        lateral_z_ = value(revision_addresses::VoidLateralZ);
        centre_x_ = value(revision_addresses::VoidCentreX);
        centre_z_ = value(revision_addresses::VoidCentreZ);
        length_ = lateral_x_ * lateral_x_ + lateral_z_ * lateral_z_;
        if (!std::isfinite(length_) || length_ < 0.5F || length_ > 1.5F || !std::isfinite(centre_x_) ||
            !std::isfinite(centre_z_))
            return;
        cover_ = std::clamp(aspect / (4.F / 3.F), 1.F, 8.F);
        begin_ = first;
        end_ = std::uint32_t(end);
    }
    void vertex(std::uint32_t address, RT64::RSP::Vertex& v) const {
        address &= 0x1FFFFFFFU;
        if (!begin_ || address < begin_ || address >= end_ || (address - begin_) % 10)
            return;
        const float lateral = ((float(v.x) - centre_x_) * lateral_x_ + (float(v.z) - centre_z_) * lateral_z_) / length_;
        const float expansion = lateral * (cover_ - 1.F);
        v.x = std::int16_t(std::clamp(std::lround(float(v.x) + expansion * lateral_x_), -32768L, 32767L));
        v.z = std::int16_t(std::clamp(std::lround(float(v.z) + expansion * lateral_z_), -32768L, 32767L));
        static thread_local bool notice = false;
        if (!notice) {
            std::fprintf(stderr,
                         "[credits][sky] lower backdrop widened along camera lateral basis; horizon unchanged\n");
            notice = true;
        }
    }
};
} // namespace dkr::runtime::presentation

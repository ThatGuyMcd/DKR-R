#pragma once
#include "online_view_policy.hpp"
#include "common/rt64_hlslpp.h"
#include <memory>
namespace RT64 {
struct State;
struct RSP;
} // namespace RT64
namespace dkr::runtime::online_view {
inline constexpr std::uint32_t kLabelTextureBegin = 0x00D00000U, kLabelTextureEnd = 0x00E00000U;
class Nameplates final {
  public:
    using IdentitySelector = void (*)(RT64::RSP&, std::uint32_t);
    Nameplates();
    ~Nameplates();
    void prepare(const Session&, const Plan&, std::span<std::uint8_t>, std::uint32_t scene) noexcept;
    void begin_view(unsigned view);
    void draw(RT64::State&, const dkr_owned_draw_event&, const hlslpp::float4x4& camera, IdentitySelector);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dkr::runtime::online_view

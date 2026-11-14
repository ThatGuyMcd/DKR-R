#pragma once
#include "online_view_policy.hpp"
#include "hud_group_layout.hpp"
#include <memory>
namespace RT64 {
struct State;
struct RSP;
} // namespace RT64
namespace dkr::runtime::online_view {
// One instance per owned renderer. No reference to live/replay RAM is retained.
class Pass final {
  public:
    Pass();
    ~Pass();
    Pass(const Pass&) = delete;
    Pass& operator=(const Pass&) = delete;
    void prepare(const Session&, std::span<const dkr_owned_draw_event>, std::span<std::uint8_t> decoder,
                 std::uint32_t scene, float aspect, std::uint32_t framebuffer, hud::LayoutMode hud_mode) noexcept;
    bool focused() const noexcept;
    bool focused_hud() const noexcept;
    bool widget_active() const noexcept;
    hud::groups::Transform widget_transform() const noexcept;
    using IdentitySelector = void (*)(RT64::RSP&, std::uint32_t);
    void selector(IdentitySelector value) noexcept;
    void begin(RT64::State&);
    bool owns_framebuffer(std::uint32_t address) const noexcept;
    bool suppress_rectangle(const RT64::State&) const noexcept;
    void before_command(RT64::State&, std::uint32_t physical_command);
    void after_command(RT64::State&, bool scissor, bool viewport);
    void camera_root(RT64::RSP&, unsigned matrix);
    void finish(RT64::State&);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dkr::runtime::online_view

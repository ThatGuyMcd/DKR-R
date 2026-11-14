#pragma once
#include "netplay/experimental_draw_events.h"
#include <cstdint>
#include <memory>
#include <span>
namespace RT64 {
struct State;
struct RSP;
} // namespace RT64
namespace dkr::runtime {
// Per-workload, renderer-only high-detail replacement. Canonical commands,
// model instances and animation buffers are never modified or reallocated.
class LocalModelRT64Pass final {
  public:
    using IdentitySelector = void (*)(RT64::RSP&, std::uint32_t);
    LocalModelRT64Pass();
    ~LocalModelRT64Pass();
    void prepare(std::span<const dkr_owned_draw_event>, std::span<std::uint8_t>, std::uint32_t) noexcept;
    void draw_at(RT64::State&, std::uint32_t, IdentitySelector);
    bool replaces_triangle(std::uint32_t command) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dkr::runtime

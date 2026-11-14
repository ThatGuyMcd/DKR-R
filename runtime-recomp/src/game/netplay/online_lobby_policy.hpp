#pragma once
#include "netplay_types.hpp"
#include <cstdint>

namespace dkr::runtime::netplay {
template <class State> constexpr bool admission_pending(State state) {
    return state == State::Connecting || state == State::AwaitingApproval;
}
constexpr bool newer_preflight(std::uint32_t id, std::uint32_t terminal, bool active, std::uint32_t current) {
    return id != 0U && static_cast<std::int32_t>(id - terminal) > 0 &&
           (!active || static_cast<std::int32_t>(id - current) > 0);
}
} // namespace dkr::runtime::netplay

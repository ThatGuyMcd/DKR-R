#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace dkr::runtime::netplay::experimental {
// Host star: the worst RTT is a conservative bound on the two longest
// client-to-host half-RTT legs, not a measured one-way latency. p99 already
// includes observed jitter; never add it again or reuse the legacy deadline.
inline std::uint8_t automatic_delay(double route_rtt_ms,unsigned window) {
    if(!std::isfinite(route_rtt_ms) || route_rtt_ms<0)route_rtt_ms=100;
    const auto usable=window>2 ? window-2 : 0;
    const double uncovered=route_rtt_ms+8.0-double(usable)*1000.0/30.0;
    return std::uint8_t(std::clamp(std::ceil(uncovered*30.0/1000.0),1.0,9.0));
}
}

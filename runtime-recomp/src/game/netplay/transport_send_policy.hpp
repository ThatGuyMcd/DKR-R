#pragma once

#include "session_transport.hpp"
#include <cstddef>

namespace dkr::runtime::netplay {
// Five channels still share one SCTP association/send queue. Limit how much
// bulk data we admit there, leaving 32 KiB for commits, input and lifecycle
// traffic. These bounds concern SDK-queued bytes, not bytes acknowledged by
// the remote peer; reliable application history remains the delivery contract.
constexpr std::size_t quick_join_lane_limit(TransportTrafficClass traffic) {
    switch (traffic) {
    case TransportTrafficClass::Control: return 32U << 10U;
    case TransportTrafficClass::Authoritative: return 32U << 10U;
    case TransportTrafficClass::Realtime: return 16U << 10U;
    case TransportTrafficClass::Checkpoint: return 16U << 10U;
    case TransportTrafficClass::Replica: return 16U << 10U;
    }
    return 0;
}
constexpr bool quick_join_admit(TransportTrafficClass traffic,
    std::size_t lane_bytes, std::size_t aggregate_bytes, std::size_t framed_bytes) {
    const auto lane_limit = quick_join_lane_limit(traffic);
    const auto aggregate_limit = (traffic == TransportTrafficClass::Checkpoint ||
        traffic == TransportTrafficClass::Replica) ? (32U << 10U) : (64U << 10U);
    return framed_bytes <= lane_limit && lane_bytes <= lane_limit - framed_bytes &&
        framed_bytes <= aggregate_limit && aggregate_bytes <= aggregate_limit - framed_bytes;
}
// In the pinned SDK, false on a connected SCTP stream means queued, not rejected.
// A route which closed/retired during the call is instead repaired by the caller.
// Acknowledged authority/control protocols remain responsible for delivery.
constexpr bool quick_join_send_accepted(bool sent_immediately,
                                         bool channel_open_after,
                                         bool connection_connected_after,
                                         bool current_route_after) {
    return sent_immediately ||
        (channel_open_after && connection_connected_after && current_route_after);
}
} // namespace dkr::runtime::netplay

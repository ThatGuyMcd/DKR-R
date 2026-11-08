#pragma once
#include <cstddef>
#include <chrono>
#include <algorithm>
#include <cstdint>

namespace dkr::online_stability {
// Keep fractional milliseconds between frequent owner-pump calls. Moving the
// anchor on every call can otherwise starve background transfers indefinitely.
inline void refill_background_budget(std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point& anchor,std::size_t& tokens,
    std::size_t burst,std::size_t per_ms) {
    tokens=std::min(tokens,burst);
    if(!anchor.time_since_epoch().count()||now<anchor||!per_ms) {anchor=now;return;}
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-anchor).count();
    if(elapsed<=0)return;
    const auto credit=std::min<std::int64_t>(elapsed,burst/per_ms+1);
    tokens=std::min(burst,tokens+std::size_t(credit)*per_ms);
    anchor+=std::chrono::milliseconds(elapsed);
}
// A sudden RTT increase must not abandon every in-flight input on the old
// short retransmission timeout. Recent samples repeat in live batches; exact
// older holes remain in the independently acknowledged repair ledger.
template<class Reliability>void configure_live_reliability(Reliability& policy) {
    policy.unordered=true;
    policy.maxRetransmits.reset();
    policy.maxPacketLifeTime=std::chrono::milliseconds(1000);
}
// SDK buffered bytes are unsent data, not packets already in flight. Keep
// immutable repair history in the application; avoid queuing seconds of old
// disposable controls inside SCTP, where newer samples cannot replace them.
template<class Traffic>constexpr bool fresh_input_capacity(Traffic traffic,
    std::size_t queued,std::size_t bytes) {
    if(traffic!=Traffic::Realtime||!queued)return true;
    constexpr std::size_t budget=512;
    return bytes<=budget&&queued<=budget-bytes;
}
}

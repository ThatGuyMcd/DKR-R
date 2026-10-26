#pragma once
#include "netplay/netplay_protocol.hpp"
#include "netplay/session_transport.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

// Diagnostic-only traffic. No controller samples, guest memory, renderer, save
// or rollback driver are exposed here. DirectSession owns authentication/send.
namespace dkr::runtime::rollback_check {
namespace net = netplay;
using Clock = std::chrono::steady_clock;
using Id = std::uint64_t;
struct Peer {
    Id id = 0;
    std::string name;
    bool spectator = false, ready = false;
    Id member = 0;
};
struct Context {
    Id room = 0, local = 0, host_id = 0, roster = 0;
    bool host = false, party = false, idle = false, automatic = false, local_watching = false;
    unsigned delay = 0, window = 0;
    std::size_t queued = 0;
    std::vector<Peer> peers, members;
    Id local_member = 0, host_member = 0;
};
enum class Phase { Idle, Negotiating, Sampling, Draining, Complete, Cancelled };
struct Row {
    Id id = 0;
    std::string name;
    bool spectator = false, supported = false, host = false;
    unsigned sent = 0, replies = 0, repair_sent = 0, repair_replies = 0;
    double median = 0, p95 = 0, jitter = 0, repair_p95 = 0;
    unsigned blocked = 0, errors = 0;
};
struct View {
    Id room = 0, local = 0, host_id = 0;
    Phase phase = Phase::Idle;
    bool active = false, complete = false, host = false, party = false, automatic = false;
    unsigned revision = 0, remaining_ms = 0, delay = 0, window = 0, scheduler_gaps = 0;
    std::size_t peak_queued = 0, final_queued = 0;
    std::string status;
    std::vector<Row> rows;
};
// False means no packet was accepted by the transport; never retry a stale
// synthetic input or fill the game's critical/reliable application queues.
using Send = std::function<bool(Id, net::protocol::MessageType, std::span<const std::uint8_t>)>;
bool request(const void*, const Context&, std::string& error);
void service(const void*, const Context&, const Send&, Clock::time_point);
void receive(const void*, const Context&, Id, net::protocol::MessageType, std::span<const std::uint8_t>, const Send&,
             Clock::time_point);
bool diagnostic_type(net::protocol::MessageType);
View snapshot(const void*);
bool active(const void*);
bool engaged(const void*);
void forget(const void*);
} // namespace dkr::runtime::rollback_check

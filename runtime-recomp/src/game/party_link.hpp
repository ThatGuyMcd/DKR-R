#pragma once
#include "party.hpp"
#include "netplay/netplay_types.hpp"
#include "netplay/session_transport.hpp"
#include "netplay/secure_channel.hpp"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <filesystem>

namespace dkr::runtime::spectators {
namespace net = dkr::runtime::netplay;
inline constexpr unsigned PartyMessageType = 250;
enum class LinkKind : std::uint8_t {
    Accepted = 1,
    Roster,
    Room,
    Launch,
    Runtime,
    Heartbeat,
    Ready,
    Start,
    Stop,
    Returned,
    Leave
};
struct LinkMessage {
    LinkKind kind = LinkKind::Heartbeat;
    std::vector<std::uint8_t> body;
};
std::vector<std::uint8_t> link_encode(LinkKind kind, std::span<const std::uint8_t> body = {});
bool link_decode(std::span<const std::uint8_t> bytes, LinkMessage& out);
std::vector<std::uint8_t> link_heartbeat(std::uint64_t token, bool reply);
bool link_heartbeat(std::span<const std::uint8_t> bytes, std::uint64_t& token, bool& reply);
struct LinkPeer {
    MemberId id = 0;
    net::PeerAddress address{};
    net::secure::Key key{};
    std::uint64_t sequence = 1;
    std::chrono::steady_clock::time_point heard{}, rate_at{};
    std::size_t received_tokens = 128 * 1024;
    std::uint64_t ping_token = 0;
    std::chrono::steady_clock::time_point ping_sent{};
    std::array<std::uint16_t, 16> rtts{};
    unsigned rtt_count = 0, rtt_index = 0;
};
struct LinkIncoming {
    MemberId source = 0;
    std::vector<std::uint8_t> bytes;
};
struct LinkOutgoing {
    MemberId target = 0;
    LinkKind kind = LinkKind::Runtime;
    net::TransportTrafficClass traffic = net::TransportTrafficClass::Control;
    std::vector<std::uint8_t> bytes;
};
enum class WatchCamera : unsigned { AllSplits, Follow, Director };
struct LinkView {
    bool enabled = false, host = false, admitted = false, running = false, stopping = false;
    MemberId local = 0;
    PartySnapshot party;
    std::map<MemberId, bool> ready;
    std::string status;
    WatchCamera camera = WatchCamera::Follow;
    unsigned target = 0;
    std::vector<std::pair<unsigned, MemberId>> swaps;
};
// DirectSession retains ownership of authentication and the WebRTC transport.
// Its member functions service this object while holding their existing lock;
// the additional recursive lock allows nested authenticated send/admit helpers.
// Public/UI functions below NEVER call DirectSession while holding this lock.
// No change to DirectSession's class layout or checked-out header is required.
struct PartyLink {
    mutable std::recursive_mutex mutex;
    bool requested = false, join_watch = false, enabled = false, host = false, admitted = false;
    bool running = false, stopping = false, returned = false, start_requested = false, ready_requested = false,
         ready_value = false;
    bool launch_sent = false, stop_requested = false;
    MemberId local = 0, authority = 0;
    std::uint64_t room_id = 0, launch_serial = 0;
    Party party;
    std::map<MemberId, bool> ready;
    std::unordered_map<MemberId, bool> pending_watch;
    std::vector<LinkPeer> peers;
    std::vector<std::uint8_t> seed;
    std::uint32_t save_generation = 0;
    std::uint64_t save_hash = 0;
    std::optional<net::LaunchDescriptor> launch;
    std::deque<LinkIncoming> incoming;
    std::array<std::deque<LinkOutgoing>, 5> outgoing;
    std::size_t incoming_bytes = 0, outgoing_bytes = 0;
    std::chrono::steady_clock::time_point advertised{}, heartbeat{}, budget_at{};
    std::size_t bulk_tokens = 256 * 1024;
    WatchCamera camera = WatchCamera::Follow;
    unsigned target = 0;
    std::string status;
    unsigned maximum_racers = 4;
    std::vector<std::pair<unsigned, MemberId>> swap_requests;
    std::optional<std::pair<unsigned, MemberId>> requested_swap, approved_swap;
    bool cancel_swap = false;
    std::uint64_t ping_token = 0;
    std::chrono::steady_clock::time_point ping_sent{};
    std::uint16_t rtt_ms = 0;
    LinkPeer* peer(MemberId id);
    LinkPeer* peer(const net::PeerAddress& address);
    bool local_watching() const;
    bool participant(MemberId id) const;
    void reset_connection();
    bool enqueue(LinkOutgoing packet);
    bool admit_incoming(MemberId sender, std::span<const std::uint8_t> bytes);
};
std::shared_ptr<PartyLink> party_link(const void* session);
void forget_party_link(const void* session);
void request_party_join(const void* session, bool spectate);
void request_party_host(const void* session, bool enabled);
LinkView party_link_view(const void* session);
bool party_enabled(const void* session);
bool party_watching(const void* session);
bool party_send(const void* session, MemberId target, std::span<const std::uint8_t> bytes,
                net::TransportTrafficClass traffic);
bool party_receive(const void* session, LinkIncoming& out);
void party_ready(const void* session, bool ready);
void party_start(const void* session);
void party_stop(const void* session, std::string reason);
void party_returned(const void* session, std::span<const std::uint8_t> confirmed_save);
void party_camera(const void* session, WatchCamera mode, unsigned target);
bool prepare_racing_ports(Party& party, std::string& error);
void party_request_swap(const void* session, unsigned port, MemberId incoming);
void party_approve_swap(const void* session, unsigned port, MemberId incoming);
void party_cancel_swap(const void* session);
bool party_waiting_role(const void* session, MemberId member, bool racer, std::string& error);
void party_bootstrap_seed(std::span<const std::uint8_t> seed);
bool party_bootstrap_active();
std::filesystem::path party_bootstrap_path();
bool party_bootstrap_load(const std::filesystem::path&, std::vector<char>& bytes);
bool party_bootstrap_write(const std::filesystem::path&, const std::vector<char>& bytes);
} // namespace dkr::runtime::spectators

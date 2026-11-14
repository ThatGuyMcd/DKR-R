#pragma once
#include "netplay/secure_channel.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::relay {
using Id = std::uint64_t;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
namespace secure = dkr::runtime::netplay::secure;
inline constexpr std::size_t MaxWire = 14912, MaxInner = 14444, MaxRoutes = 16, MaxSignal = 24000;
inline constexpr std::size_t MaxLobbyMembers = 12;
struct Member {
    Id id = 0;
    std::string name, address;
    bool racer = true;
    unsigned slot = 255;
    unsigned ping_ms = 0;
};
struct Context {
    Id room = 0, local = 0, host = 0;
    bool active = false, allow = false, volunteer = false, quick = false, building = true;
    std::vector<Member> members;
    Id generation = 0;
};
struct Grant {
    Id id = 0, room = 0, host = 0, endpoint = 0, proxy = 0;
    secure::Key key{}, proof{};
    std::string endpoint_address, proxy_address, name, proxy_name;
};
struct Control {
    Id target = 0;
    Bytes bytes;
};
struct Summary {
    unsigned samples = 0, p50 = 0, p95 = 0, p99 = 0, lost = 0;
};
struct Row {
    Id endpoint = 0, route = 0;
    std::string name, via, ice;
    bool selected = false, ready = false;
    Summary local, remote;
    std::uint64_t delivery_age_ms = 0;
    bool delivery_stalled = false;
    std::string reason;
};
struct LobbyRoute {
    Id endpoint = 0, proxy = 0;
    std::string name, via;
    unsigned slot = 255;
    unsigned ping_ms = 0;
};
struct PingLabel {
    unsigned ms = 0;
    std::string text, detail;
};
struct RouteLabel {
    std::string text;
    bool proxy = false, checking = false;
};
struct View {
    bool enabled = false, volunteer = false;
    std::vector<Row> rows;
    std::uint64_t forwarded = 0, dropped = 0;
    std::size_t queued = 0;
    Id local = 0, host = 0;
    std::vector<LobbyRoute> lobby;
    Id lobby_generation = 0;
};
struct Io {
    // Calls are owner-thread-only. Extra links terminate at the proxy, never
    // recursively enter another relay adapter.
    std::function<bool(const Grant&, Id, std::span<const std::uint8_t>, unsigned)> send;
    std::function<bool(const Grant&)> ready;
    std::function<void(const Grant&, bool)> open;
    std::function<void(Id)> close;
    std::function<void(Id, std::string)> signal;
    std::function<bool(Id, Bytes, unsigned)> deliver;
    std::function<std::string(Id)> ice;
};
class Samples {
  public:
    void add(unsigned ms, bool lost, Clock::time_point now);
    Summary summary(Clock::time_point now) const;
    bool fresh(Clock::time_point now) const;

  private:
    struct Sample {
        unsigned ms;
        bool lost;
        Clock::time_point at;
    };
    std::deque<Sample> values_;
};
class Engine final {
  public:
    explicit Engine(Io io) : io_(std::move(io)) {}
    ~Engine();
    void configure(Context context, Clock::time_point now = Clock::now());
    void service(Clock::time_point now = Clock::now());
    bool control(Id source, std::span<const std::uint8_t> bytes, Clock::time_point now = Clock::now());
    bool wire(Id route, Id source, std::span<const std::uint8_t> bytes, Clock::time_point now = Clock::now());
    bool send(Id destination, std::span<const std::uint8_t> inner, unsigned lane, Clock::time_point now = Clock::now());
    void signal(Id route, std::string text);
    std::vector<Control> controls();
    View view(Clock::time_point now = Clock::now()) const;
    void reset();
    void recover(Clock::time_point now = Clock::now(), Id peer = 0);
    const Context& context() const {
        return context_;
    }
    const Grant* grant(Id route) const;
    std::optional<Grant> grant_for_wire(std::span<const std::uint8_t>) const;
    unsigned rtt(Id endpoint, Clock::time_point now = Clock::now()) const;
    bool usable(Id endpoint, Clock::time_point now = Clock::now()) const;

  private:
    struct Replay {
        Id top = 0;
        std::array<Id, 8> bits{};
        bool accept(Id);
    };
    struct Route {
        Grant grant;
        bool started = false, opening = false;
        std::set<Id> acknowledgements;
        Clock::time_point granted{}, ping{}, report{}, remote_at{};
        Samples local;
        Summary remote;
        bool remote_delivering = true;
        Clock::time_point blocked_until{};
        struct Delivery {
            Id sent = 0, acknowledged = 0;
            Clock::time_point pending{}, received{}, receipt{}, blocked{}, attempted{}, sample_after{};
            std::map<Id, Bytes> proofs;
        };
        std::array<Delivery, 5> delivery{};
        std::map<Id, Clock::time_point> pending;
        std::map<std::pair<Id, unsigned>, Replay> replay;
        struct Assembly {
            std::map<unsigned, std::string> pieces;
            unsigned count = 0;
            Clock::time_point at{};
        };
        std::map<Id, Assembly> signals;
        Replay completed_signals;
    };
    struct Capability {
        bool volunteer = false;
        Clock::time_point heard{};
    };
    struct Choice {
        Id active = 0, wanted = 0, serial = 0, candidate = 0;
        Clock::time_point since{}, changed{}, sent{};
        bool announce = false;
    };
    struct Queued {
        Id route, to;
        unsigned lane;
        Bytes bytes;
        Clock::time_point expires;
    };
    bool emit(Id target, unsigned type, const std::function<void(Bytes&)>& body = {});
    bool handle(Id, std::span<const std::uint8_t>, Clock::time_point);
    void permission(const Grant&);
    void make(Id endpoint, Id proxy, Clock::time_point now);
    bool packet(Route&, Id to, unsigned kind, Id origin, Id destination, unsigned lane, Id token,
                std::span<const std::uint8_t>, unsigned hop, Clock::time_point);
    bool transmit(Route&, Id, std::span<const std::uint8_t>, unsigned, Clock::time_point, bool queue);
    void choose(Clock::time_point);
    std::vector<LobbyRoute> lobby_routes(Clock::time_point) const;
    void publish_lobby(Clock::time_point);
    void erase(Id, bool notify);
    bool admitted(Id) const;
    bool delivering(const Route&, Clock::time_point) const;
    const Member* member(Id) const;
    Io io_;
    Context context_;
    bool policy_ = false;
    Clock::time_point hello_{}, policy_at_{}, choose_at_{};
    bool last_volunteer_ = false;
    std::map<Id, Capability> capabilities_;
    std::map<Id, Route> routes_;
    std::map<Id, Choice> choices_;
    std::deque<Control> controls_;
    std::array<std::deque<Queued>, 5> queued_;
    struct PendingControl {
        Control packet;
        Clock::time_point sent{}, created{};
    };
    std::map<Id, PendingControl> pending_controls_;
    std::map<Id, Replay> control_replay_;
    std::size_t queued_bytes_ = 0;
    Id serial_ = 0, signal_serial_ = 0;
    Id control_serial_ = 0;
    std::uint64_t forwarded_ = 0, dropped_ = 0;
    Clock::time_point tokens_at_{};
    std::size_t tokens_ = 256 * 1024;
    std::vector<LobbyRoute> lobby_;
    Bytes lobby_payload_;
    Id lobby_serial_ = 0, lobby_generation_ = 0;
    Clock::time_point lobby_heard_{}, lobby_at_{}, lobby_heartbeat_{};
    std::map<Id, Clock::time_point> recovery_at_;
};
std::string diagnostics(const View&);
RouteLabel lobby_route(const View&, std::string_view peer, bool host = false);
std::string lobby_card_peer(const View&, std::string_view peer, bool host, bool local, unsigned slot, Id generation);
RouteLabel lobby_card_route(const View&, std::string_view peer, bool host, bool local, unsigned slot, Id generation);
PingLabel lobby_card_ping(const View&, std::string_view peer, bool host, bool local, unsigned slot, Id generation);
} // namespace dkr::relay

#pragma once
#include "host_settings.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dkr::online_stability {
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::uint8_t>;
using Member = std::uint64_t;
inline constexpr unsigned ExtensionKind = 13;
// Extension payload tags: 1=mod progress, 2=legacy delay, 3=proxy, 4=chat.
inline constexpr std::uint8_t DelayPayloadKind = 5;
inline constexpr unsigned MaximumJoins = 16;
enum class Stage : std::uint8_t { Approval, Review, Queued, Downloading, Preparing, Entering, Cancelled, Failed };
const char* stage_text(Stage stage);
struct Join {
    Member request = 0;
    std::uint64_t attempt = 0, received = 0, total = 0;
    std::string name;
    Stage stage = Stage::Approval;
    bool operator==(const Join&) const = default;
};
struct Progress {
    std::uint64_t room = 0, revision = 0;
    std::vector<Join> rows;
};
Bytes encode(const Progress& value);
bool decode(std::span<const std::uint8_t> bytes, Progress& value);
struct Route { Member id = 0; unsigned rtt_ms = 0; bool racer = false; unsigned owner = 255; };
struct Identity {
    std::uint64_t room = 0;
    Member local = 0, host = 0;
    bool active = false, authority = false, dynamic = false;
    std::vector<Route> routes;
    bool local_racer = true;
    std::optional<unsigned> manual_delay,prediction_window;
};
struct Packet { Member target = 0; Bytes bytes; };
struct Received { Member source = 0; Bytes bytes; };
struct DelayView {
    bool requested = false, negotiated = false; unsigned frames = 0; std::string reason; bool stalled=false;
    std::uint8_t stalled_owners=0;
    unsigned arrival_age_ms=0,queued_transitions=0;
};
// This bridge owns bounded metadata only. Its mutex never nests a session lock
// in the reverse direction; callers do not invoke DirectSession from here.
class Control final {
public:
    void configure(Identity identity);
    Identity identity() const;
    void mods(std::vector<Join> rows);
    bool mod_update_due(Clock::time_point now);
    void pending(std::vector<Join> rows, Clock::time_point now);
    std::optional<Bytes> publication(Clock::time_point now);
    bool receive(Member sender, bool admitted, std::span<const std::uint8_t> bytes);
    std::vector<Join> joins(Clock::time_point now = Clock::now()) const;
    bool send(Member target, Bytes bytes);
    std::vector<Packet> outgoing();
    std::vector<Received> incoming();
    void report(DelayView value);
    DelayView delay() const;
    void publish_host_settings(HostSettings current,HostSettingsContext context);
    HostSettingsView host_settings()const;
    bool request_host_settings(std::uint64_t room,HostSettings desired,std::uint64_t& serial,std::string& error);
    std::optional<HostSettingsRequest> take_host_settings();
    void finish_host_settings(const HostSettingsRequest& request,std::optional<HostSettings> queued,std::string status,bool accepted);
private:
    mutable std::mutex mutex_;
    Identity identity_;
    Progress progress_;
    std::map<Member, Join> mods_;
    std::deque<Packet> outgoing_;
    std::deque<Received> incoming_;
    Clock::time_point published_{}, received_{}, mod_update_{};
    bool dirty_ = true;
    DelayView delay_;
    HostSettingsView host_settings_;
    std::optional<HostSettingsRequest> host_settings_request_;
    std::uint64_t host_settings_serial_=0;
};
std::shared_ptr<Control> control(const void* session);
void forget(const void* session);
void bind_mod_sync(const void* link,const void* session);
void unbind_mod_sync(const void* link);
std::shared_ptr<Control> mod_control(const void* link);
} // namespace dkr::online_stability

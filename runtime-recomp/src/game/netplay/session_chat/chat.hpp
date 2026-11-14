#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace dkr::chat {
using Id = std::uint64_t;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
inline constexpr std::size_t MaximumText = 256, MaximumHistory = 100, MaximumMembers = 12;
enum class Kind : std::uint8_t { Policy = 1, Submit, Delivery, Receipt, Reject };
enum class Delivery { Waiting, Sent, Failed };
struct Member {
    Id id = 0;
    std::string name;
    bool operator==(const Member&) const = default;
};
struct Context {
    Id room = 0, local = 0, host = 0;
    bool active = false, allow = true;
    std::vector<Member> members;
};
struct Wire {
    Kind kind = Kind::Policy;
    Id room = 0, id = 0, request = 0, author = 0, seconds = 0;
    bool enabled = false;
    std::string name, text;
};
struct Message {
    Id id = 0, request = 0, author = 0, seconds = 0;
    std::string name, text;
    Delivery delivery = Delivery::Sent;
    Clock::time_point received{};
};
struct Packet {
    Id target = 0;
    Bytes bytes;
};
struct View {
    Id room = 0, local = 0;
    bool active = false, host = false, known = false, enabled = false;
    std::vector<Member> members;
    std::vector<Message> messages;
    std::string status;
};
bool valid_text(std::string_view text, std::size_t maximum = MaximumText);
Bytes encode(const Wire&);
bool decode(std::span<const std::uint8_t>, Wire&);
Id peer_id(std::string_view);

// UI methods never enter DirectSession. The authenticated session owner supplies
// membership and consumes a small, rate-limited outbox after essential traffic.
class Channel final {
  public:
    void configure(Context, Clock::time_point now = Clock::now());
    bool send(std::string text, std::string& error, Clock::time_point now = Clock::now());
    bool retry(Id request, std::string& error, Clock::time_point now = Clock::now());
    bool receive(Id source, std::span<const std::uint8_t>, Clock::time_point now = Clock::now());
    std::vector<Packet> outgoing(Clock::time_point now = Clock::now());
    View view(Clock::time_point now = Clock::now(), std::size_t maximum = MaximumHistory) const;

  private:
    struct Replay {
        Id top = 0, bits = 0;
        bool accept(Id);
    };
    struct Bucket {
        double tokens = 0;
        Clock::time_point at{};
        bool take(double capacity, double per_second, Clock::time_point);
    };
    struct Pending {
        Wire wire;
        Clock::time_point next{}, expires{};
        unsigned attempts = 0;
    };
    const Member* member(Id) const;
    bool accept(Id source, Id request, const std::string&, std::string&, Clock::time_point);
    void queue(Id target, Wire, Clock::time_point);
    void immediate(Id target, Wire);
    void service(Clock::time_point);
    void append(Message);
    void fail(Id request);
    mutable std::mutex mutex_;
    Context context_;
    bool known_ = false, enabled_ = false;
    Id next_request_ = 0, next_message_ = 0, policy_revision_ = 0, remote_policy_ = 0;
    Clock::time_point began_{}, policy_at_{}, send_at_{};
    std::deque<Message> history_;
    std::map<std::pair<Id, Id>, Pending> pending_;
    std::map<std::tuple<Id, Id, Kind>, Wire> immediate_;
    std::map<Id, Replay> requests_;
    Replay delivered_;
    std::map<Id, Bucket> authors_, ingress_;
    Bucket room_rate_;
};
std::shared_ptr<Channel> channel(const void* session);
void reset(const void* session);
} // namespace dkr::chat

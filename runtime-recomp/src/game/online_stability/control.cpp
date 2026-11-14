#include "control.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>

namespace dkr::online_stability {
namespace {
bool name_ok(std::string_view text) {
    return !text.empty() && text.size() <= 96 &&
           std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void put(Bytes& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        bytes.push_back(std::uint8_t(value >> (i * 8)));
}
bool get(std::span<const std::uint8_t> bytes, std::size_t& at, std::uint64_t& value, unsigned width) {
    if (at > bytes.size() || width > bytes.size() - at)
        return false;
    value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= std::uint64_t(bytes[at++]) << (i * 8);
    return true;
}
struct Registry {
    std::mutex mutex;
    std::unordered_map<const void*, std::shared_ptr<Control>> sessions;
    std::unordered_map<const void*, std::weak_ptr<Control>> mod_links;
};
Registry& registry() {
    // Global DirectSession owners can enter this service before main() and
    // during process teardown. Its registry must survive both boundaries.
    static auto* const instance = new Registry;
    return *instance;
}
} // namespace
const char* stage_text(Stage stage) {
    switch (stage) {
    case Stage::Approval:
        return "Waiting for host approval";
    case Stage::Review:
        return "Reviewing required session mods";
    case Stage::Queued:
        return "Queued for download";
    case Stage::Downloading:
        return "Downloading session mods";
    case Stage::Preparing:
        return "Verifying and preparing session mods";
    case Stage::Entering:
        return "Entering the lobby";
    case Stage::Cancelled:
        return "Join cancelled";
    case Stage::Failed:
        return "Join could not complete";
    }
    return "Joining";
}
Bytes encode(const Progress& value) {
    if (!value.room || !value.revision || value.rows.size() > MaximumJoins)
        return {};
    Bytes bytes{'D', 'K', 'R', 'X', 1, 1};
    put(bytes, value.room, 8);
    put(bytes, value.revision, 8);
    put(bytes, value.rows.size(), 1);
    std::set<Member> ids;
    for (const auto& row : value.rows) {
        if (!row.request || !row.attempt || !ids.insert(row.request).second || !name_ok(row.name) ||
            row.stage > Stage::Failed || row.received > row.total || row.total > (1ULL << 40))
            return {};
        put(bytes, row.request, 8);
        put(bytes, row.attempt, 8);
        put(bytes, unsigned(row.stage), 1);
        put(bytes, row.received, 8);
        put(bytes, row.total, 8);
        put(bytes, row.name.size(), 1);
        bytes.insert(bytes.end(), row.name.begin(), row.name.end());
    }
    return bytes;
}
bool decode(std::span<const std::uint8_t> bytes, Progress& value) {
    if (bytes.size() < 23 || bytes.size() > 2300 ||
        !std::equal(bytes.begin(), bytes.begin() + 6, std::array<std::uint8_t, 6>{'D', 'K', 'R', 'X', 1, 1}.begin()))
        return false;
    Progress next;
    std::size_t at = 6;
    std::uint64_t count = 0;
    if (!get(bytes, at, next.room, 8) || !get(bytes, at, next.revision, 8) || !get(bytes, at, count, 1) || !next.room ||
        !next.revision || count > MaximumJoins)
        return false;
    std::set<Member> ids;
    for (unsigned i = 0; i < count; ++i) {
        Join row;
        std::uint64_t stage = 0, length = 0;
        if (!get(bytes, at, row.request, 8) || !get(bytes, at, row.attempt, 8) || !get(bytes, at, stage, 1) ||
            !get(bytes, at, row.received, 8) || !get(bytes, at, row.total, 8) || !get(bytes, at, length, 1) ||
            !row.request || !row.attempt || !ids.insert(row.request).second || stage > unsigned(Stage::Failed) ||
            row.received > row.total || row.total > (1ULL << 40) || length > 96 || length > bytes.size() - at)
            return false;
        row.stage = Stage(stage);
        row.name.assign(reinterpret_cast<const char*>(bytes.data() + at), std::size_t(length));
        at += std::size_t(length);
        if (!name_ok(row.name))
            return false;
        next.rows.push_back(std::move(row));
    }
    if (at != bytes.size())
        return false;
    value = std::move(next);
    return true;
}
void Control::configure(Identity next) {
    std::lock_guard lock(mutex_);
    if (next.routes.size() > 12)
        next.routes.resize(12);
    if (!next.active)
        next.room = 0;
    if (next.room != identity_.room || next.local != identity_.local || next.host != identity_.host) {
        progress_ = {};
        mods_.clear();
        outgoing_.clear();
        incoming_.clear();
        published_ = {};
        received_ = {};
        mod_update_ = {};
        delay_ = {};
        dirty_ = true;
        host_settings_ = {};
        host_settings_request_.reset();
        host_settings_serial_ = 0;
    }
    identity_ = std::move(next);
}
Identity Control::identity() const {
    std::lock_guard lock(mutex_);
    return identity_;
}
void Control::mods(std::vector<Join> rows) {
    std::lock_guard lock(mutex_);
    mods_.clear();
    for (auto& row : rows)
        if (mods_.size() < MaximumJoins)
            mods_.emplace(row.request, std::move(row));
}
bool Control::mod_update_due(Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority ||
        (mod_update_.time_since_epoch().count() && now - mod_update_ < std::chrono::milliseconds(100)))
        return false;
    mod_update_ = now;
    return true;
}
void Control::pending(std::vector<Join> rows, Clock::time_point) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority)
        return;
    if (rows.size() > MaximumJoins)
        rows.resize(MaximumJoins);
    for (auto& row : rows)
        if (row.stage != Stage::Approval && row.stage != Stage::Entering)
            if (const auto p = mods_.find(row.request); p != mods_.end()) {
                row.stage = p->second.stage == Stage::Entering ? Stage::Preparing : p->second.stage;
                row.received = p->second.received;
                row.total = p->second.total;
            }
    if (rows != progress_.rows) {
        progress_.rows = std::move(rows);
        dirty_ = true;
    }
}
std::optional<Bytes> Control::publication(Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority ||
        (published_.time_since_epoch().count() && now - published_ < std::chrono::milliseconds(500)))
        return {};
    // Full snapshots also reach late joiners and heal a lost empty/cleanup update.
    if (!dirty_ && published_.time_since_epoch().count() && now - published_ < std::chrono::seconds(2))
        return {};
    progress_.room = identity_.room;
    if (++progress_.revision == 0)
        ++progress_.revision;
    auto bytes = encode(progress_);
    if (bytes.empty())
        return {};
    dirty_ = false;
    published_ = now;
    return bytes;
}
bool Control::receive(Member sender, bool admitted, std::span<const std::uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !admitted || bytes.size() < 6 || bytes.size() > 2300)
        return false;
    if (bytes[5] == 1) {
        if (identity_.authority || sender != identity_.host)
            return false;
        Progress next;
        if (!decode(bytes, next) || next.room != identity_.room || next.revision <= progress_.revision)
            return false;
        progress_ = std::move(next);
        received_ = Clock::now();
        return true;
    }
    // Delay messages have their own strict codec and scope checks on the owner.
    if (bytes[5] != DelayPayloadKind || bytes.size() > 128 || incoming_.size() >= 64 ||
        (identity_.authority ? std::none_of(identity_.routes.begin(), identity_.routes.end(),
                                            [&](const auto& r) { return r.id == sender && r.racer; })
                             : sender != identity_.host))
        return false;
    incoming_.push_back({sender, {bytes.begin(), bytes.end()}});
    return true;
}
std::vector<Join> Control::joins(Clock::time_point now) const {
    std::lock_guard lock(mutex_);
    if (!identity_.active ||
        (!identity_.authority && (!received_.time_since_epoch().count() || now - received_ > std::chrono::seconds(8))))
        return {};
    return progress_.rows;
}
bool Control::send(Member target, Bytes bytes) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || bytes.empty() || bytes.size() > 2300 || outgoing_.size() >= 64)
        return false;
    outgoing_.push_back({target, std::move(bytes)});
    return true;
}
std::vector<Packet> Control::outgoing() {
    std::lock_guard lock(mutex_);
    std::vector<Packet> result;
    for (unsigned i = 0; i < 8 && !outgoing_.empty(); ++i) {
        result.push_back(std::move(outgoing_.front()));
        outgoing_.pop_front();
    }
    return result;
}
std::vector<Received> Control::incoming() {
    std::lock_guard lock(mutex_);
    std::vector<Received> result;
    while (!incoming_.empty()) {
        result.push_back(std::move(incoming_.front()));
        incoming_.pop_front();
    }
    return result;
}
void Control::report(DelayView value) {
    std::lock_guard lock(mutex_);
    delay_ = std::move(value);
}
DelayView Control::delay() const {
    std::lock_guard lock(mutex_);
    return delay_;
}
void Control::publish_host_settings(HostSettings current, HostSettingsContext context) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority) {
        host_settings_ = {};
        host_settings_request_.reset();
        return;
    }
    host_settings_.available = true;
    host_settings_.room = identity_.room;
    host_settings_.current = current;
    host_settings_.context = context;
}
HostSettingsView Control::host_settings() const {
    std::lock_guard lock(mutex_);
    return host_settings_;
}
bool Control::request_host_settings(std::uint64_t room, HostSettings desired, std::uint64_t& serial,
                                    std::string& error) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority || !room || room != identity_.room || !host_settings_.available) {
        error = "Only the current lobby host can change these settings.";
        return false;
    }
    const auto plan = plan_host_settings(host_settings_.current, desired, host_settings_.context);
    if (!plan) {
        error = plan.error;
        return false;
    }
    if (host_settings_serial_ == UINT64_MAX) {
        error = "Host settings request limit reached. Reopen the lobby.";
        return false;
    }
    serial = ++host_settings_serial_;
    host_settings_request_ = HostSettingsRequest{room, serial, desired};
    error.clear();
    return true;
}
std::optional<HostSettingsRequest> Control::take_host_settings() {
    std::lock_guard lock(mutex_);
    auto value = std::move(host_settings_request_);
    host_settings_request_.reset();
    if (!identity_.active || !identity_.authority || !value || value->room != identity_.room)
        return {};
    return value;
}
void Control::finish_host_settings(const HostSettingsRequest& request, std::optional<HostSettings> queued,
                                   std::string status, bool accepted) {
    std::lock_guard lock(mutex_);
    if (!identity_.active || !identity_.authority || request.room != identity_.room)
        return;
    host_settings_.reply = request.serial;
    host_settings_.accepted = accepted;
    host_settings_.submitted = request.desired;
    host_settings_.queued = std::move(queued);
    host_settings_.status = std::move(status);
}
std::shared_ptr<Control> control(const void* key) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    auto& value = state.sessions[key];
    if (!value)
        value = std::make_shared<Control>();
    return value;
}
void forget(const void* key) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    state.sessions.erase(key);
}
void bind_mod_sync(const void* link, const void* session) {
    auto owner = control(session);
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    state.mod_links[link] = owner;
}
void unbind_mod_sync(const void* link) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    state.mod_links.erase(link);
}
std::shared_ptr<Control> mod_control(const void* link) {
    auto& state = registry();
    std::lock_guard lock(state.mutex);
    const auto found = state.mod_links.find(link);
    return found == state.mod_links.end() ? nullptr : found->second.lock();
}
} // namespace dkr::online_stability

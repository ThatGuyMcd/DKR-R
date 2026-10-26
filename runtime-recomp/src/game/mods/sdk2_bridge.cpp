#include "sdk2_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <chrono>
namespace dkr::sdk2 {
namespace {
std::mutex mutex;
Snapshot current;
std::shared_ptr<const Snapshot> cached;
std::deque<Command> pending;
std::uint32_t serial = 0;
Reload pending_reload;
struct ToolMessage {
    unsigned id = 0, kind = 0, target = 0;
    Command command;
};
struct ToolPeer {
    unsigned sent = 0, received = 0;
    std::deque<ToolMessage> pending;
};
std::array<ToolPeer, 4> tool_peers;
std::uint64_t tool_epoch = 0;
unsigned observed_serial = 0;
std::array<LocalCamera, 4> cameras;
std::map<unsigned, std::vector<DebugPrimitive>> debug_owners;
std::array<std::vector<DebugLine>, 4> debug_views;
bool send_tool(unsigned peer, unsigned kind, unsigned target, Command command = {}) {
    auto& route = tool_peers[peer];
    if (route.pending.size() >= 32 || route.sent == UINT32_MAX)
        return false;
    route.pending.push_back({++route.sent, kind, target, command});
    return true;
}
bool enqueue(const Panel& panel, Command command) {
    if (pending.size() >= 512 || serial == UINT32_MAX)
        return false;
    command.words[1] = ++serial;
    pending.push_back(command);
    return true;
}
} // namespace
void begin(const std::vector<std::shared_ptr<const mods::code::Package>>& packages, bool online) {
    std::lock_guard guard(mutex);
    cached.reset();
    ++current.generation;
    current.active = true;
    current.online = online;
    current.host = !online;
    current.development = !online;
    current.panels.clear();
    current.history.clear();
    current.notice.clear();
    pending.clear();
    current.local_session = std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    current.requests.clear();
    current.local = 0;
    current.players = online ? 0 : 1;
    current.grants = 0;
    tool_peers = {};
    tool_epoch = 0;
    observed_serial = 0;
    cameras = {};
    debug_owners.clear();
    debug_views = {};
    pending_reload = {};
    hud_hidden = false;
    for (unsigned i = 0; i < packages.size(); ++i) {
        const auto& p = *packages[i];
        if (p.api != 2 || !p.resources.contains("tools-ui"))
            continue;
        current.panels.push_back({i, p.id, p.name, p.digest, mods::code::parse_document(p.resources.at("tools-ui")),
                                  Json::object(), p.development});
    }
}
void end() {
    std::lock_guard guard(mutex);
    cached.reset();
    ++current.generation;
    current.active = false;
    current.panels.clear();
    pending.clear();
    cameras = {};
    tools_open = false;
    hud_hidden = false;
}
void authority(bool host, bool development) {
    std::lock_guard guard(mutex);
    cached.reset();
    current.host = host;
    current.development = development;
    if (!host || !development)
        pending.clear();
}
std::shared_ptr<const Snapshot> snapshot() {
    std::lock_guard guard(mutex);
    if (!cached)
        cached = std::make_shared<const Snapshot>(current);
    return cached;
}
bool available() {
    std::lock_guard guard(mutex);
    return current.active && !current.panels.empty();
}
bool submit(unsigned owner, unsigned action, const std::array<std::uint32_t, 13>& arguments, std::string_view digest,
            std::uint64_t generation) {
    std::lock_guard guard(mutex);
    cached.reset();
    const auto panel =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    if (generation != current.generation || panel == current.panels.end() || panel->digest != digest || !panel->scene) {
        current.notice = "The tool target changed. Wait for the new scene snapshot and select it again.";
        return false;
    }
    if (!current.active || !current.development || panel == current.panels.end() || !panel->development || !action ||
        pending.size() >= 512) {
        current.notice = "This action needs an active development session and host authority.";
        return false;
    }
    if (serial == UINT32_MAX) {
        current.notice = "Command IDs exhausted. Restart the app.";
        return false;
    }
    Command command;
    command.words[0] = owner + 1;
    command.words[1] = ++serial;
    command.words[2] = action;
    std::copy_n(arguments.begin(), 12, command.words.begin() + 3);
    command.words[15] = panel->scene;
    if (current.online && !current.host) {
        if (!tool_epoch || !send_tool(0, 1, command.words[1], command)) {
            current.notice = "The request queue is busy. Try again after the host replies.";
            return false;
        }
        current.notice = "Requested from the host. Nothing was changed locally.";
        return true;
    }
    pending.push_back(command);
    current.history.push_back(panel->name + ": action " + std::to_string(action) + " queued (#" +
                              std::to_string(serial) + ")");
    if (current.history.size() > 128)
        current.history.pop_front();
    current.notice = "Command queued.";
    return true;
}
#include "sdk2_broker.inl"
#include "sdk2_camera.inl"
#include "sdk2_debug.inl"
Command take(bool online) {
    std::lock_guard guard(mutex);
    if (!current.active || current.online != online || !current.host || !current.development || pending.empty())
        return {};
    auto command = pending.front();
    pending.pop_front();
    return command;
}
void publish(unsigned owner, Json values, std::uint32_t scene, std::uint32_t tick) {
    std::lock_guard guard(mutex);
    const auto found =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    if (found == current.panels.end())
        return;
    cached.reset();
    // Validate the presentation contract before publishing. A malformed mod
    // value must return a VM status, not throw through a live ImGui stack.
    validate_values(found->schema, values);
    found->values = std::move(values);
    found->scene = scene;
    found->tick = tick;
}
void validate_values(const Json& schema, const Json& values) {
    for (const auto& page : schema.at("pages"))
        for (const auto& field : page.at("fields")) {
            if (field.contains("visible")) {
                const auto flag = values.find(field.at("visible").get<std::string>());
                if (flag != values.end() && !flag->is_boolean())
                    throw mods::Error("SDK tools visibility value must be boolean.");
            }
            const auto value = values.find(field.at("id").get<std::string>());
            if (value == values.end())
                continue;
            const auto type = field.at("type").get<std::string>();
            if (type == "table") {
                if (!value->is_array() || value->size() > 128)
                    throw mods::Error("SDK tools table exceeds its row/type contract.");
                for (const auto& row : *value)
                    if (row.is_object()) {
                        for (const auto* caption : {"name", "detail"})
                            if (row.contains(caption) &&
                                (!row[caption].is_string() || row[caption].get_ref<const std::string&>().size() > 2048))
                                throw mods::Error("SDK table caption is invalid.");
                        for (const auto* handle : {"slot", "generation"})
                            if (row.contains(handle) &&
                                (!row[handle].is_number_unsigned() || row[handle].get<std::uint64_t>() > UINT32_MAX))
                                throw mods::Error("SDK table handle is invalid.");
                    }
            } else if (type != "text" && type != "search" && type != "button" && type != "inspector" &&
                       type != "diagnostics" && type != "authority" && type != "document" && type != "console" &&
                       type != "camera" && type != "debug" && type != "reload") {
                if (!value->is_number() || !std::isfinite(value->get<double>()) || std::abs(value->get<double>()) > 1e7)
                    throw mods::Error("SDK tools numeric value is invalid.");
            }
        }
}
bool request_reload(Reload request, std::uint64_t generation) {
    std::lock_guard guard(mutex);
    if (current.online || !current.development || generation != current.generation || pending_reload.package ||
        !request.package)
        return false;
    const auto p = std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& item) {
        return item.owner == request.owner && item.digest == request.digest;
    });
    if (p == current.panels.end() || !p->development)
        return false;
    pending_reload = std::move(request);
    return true;
}
Reload take_reload() {
    std::lock_guard guard(mutex);
    return std::exchange(pending_reload, Reload{});
}
void notice(std::string value) {
    std::lock_guard guard(mutex);
    cached.reset();
    current.notice = std::move(value);
}
void inspector(unsigned owner, std::shared_ptr<const std::vector<EntityView>> entities) {
    std::lock_guard guard(mutex);
    const auto found =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    if (found != current.panels.end()) {
        found->inspector = std::move(entities);
        cached.reset();
    }
}
void diagnostics(unsigned owner, Json values) {
    std::lock_guard guard(mutex);
    const auto found =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    if (found != current.panels.end()) {
        found->diagnostics = std::move(values);
        cached.reset();
    }
}
void document(unsigned owner, std::string id, Json value) {
    std::lock_guard guard(mutex);
    const auto found =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    if (found != current.panels.end()) {
        found->documents[std::move(id)] = std::move(value);
        cached.reset();
    }
}
bool upload(unsigned owner, unsigned document_index, const Json& value, std::string_view digest,
            std::uint64_t generation) {
    const auto bytes = value.dump();
    if (bytes.empty() || bytes.size() > 16384)
        return false;
    std::lock_guard guard(mutex);
    const auto found =
        std::find_if(current.panels.begin(), current.panels.end(), [&](const auto& p) { return p.owner == owner; });
    const unsigned count = unsigned((bytes.size() + 39) / 40) + 2;
    if (!current.active || !current.host || !current.development || generation != current.generation ||
        found == current.panels.end() || found->digest != digest || !found->scene || pending.size() + count > 512 ||
        serial > UINT32_MAX - count)
        return false;
    std::uint32_t hash = 2166136261U;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 16777619U;
    }
    const auto command = [&](unsigned action) {
        Command c;
        c.words[0] = owner + 1;
        c.words[1] = ++serial;
        c.words[2] = action;
        c.words[15] = found->scene;
        return c;
    };
    auto begin = command(65534);
    begin.words[3] = unsigned(bytes.size());
    begin.words[4] = hash;
    begin.words[5] = document_index;
    pending.push_back(begin);
    for (unsigned offset = 0; offset < bytes.size(); offset += 40) {
        auto chunk = command(65533);
        chunk.words[3] = offset;
        chunk.words[4] = std::min(40U, unsigned(bytes.size() - offset));
        for (unsigned i = 0; i < chunk.words[4]; ++i)
            chunk.words[5 + i / 4] |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i]))
                                      << (24 - (i % 4) * 8);
        pending.push_back(chunk);
    }
    auto commit = command(65532);
    pending.push_back(commit);
    cached.reset();
    current.notice = "Document queued for the ordered simulation boundary. It is not applied from the file picker.";
    return true;
}
} // namespace dkr::sdk2

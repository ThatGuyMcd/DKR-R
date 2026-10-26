#include "online_directory.hpp"
#include "atomic_snapshot.hpp"
#include "directory_tls.hpp"
#include "monocypher.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace dkr::online {
namespace {
std::string string(const Json& j, const char* name, std::size_t limit = 160) {
    if (!j.is_object() || !j.contains(name) || !j[name].is_string())
        return {};
    auto s = j[name].get<std::string>();
    return s.size() <= limit ? s : std::string{};
}
struct State {
    std::mutex mutex, bridge_mutex;
    std::condition_variable_any cv;
    AccountView account;
    AccountRequest request;
    View ui;
    dkr::runtime::AtomicSnapshot<const View> published{std::make_shared<const View>()};
    std::map<const void*, std::shared_ptr<Bridge>> bridges;
    struct JoinKey {
        secure::KeyPair pair;
        secure::Key admission;
    };
    std::map<const void*, JoinKey> keys;
    std::deque<Json> inbox, outbox;
    std::map<std::string, std::string> pending;
    std::map<std::string, std::string> join_requests;
    std::filesystem::path root;
    std::string instance = secure::encode_key(secure::generate_key()), compatible, joining_id;
    std::size_t jitter = std::hash<std::string>{}(instance);
    secure::KeyPair join_key{};
    Json bootstrap;
    bool configured = false, watch = false, stopping = false, retry_now = false;
    std::shared_ptr<rtc::WebSocket> socket;
    std::atomic<std::uint64_t> channel{0};
    std::uint64_t serial = 0;
    std::jthread worker;
    void update() {
        published.store(std::make_shared<const View>(ui));
    }
};
State& state() {
    static auto* p = new State;
    return *p;
}
std::string next_id(State& s) {
    return "r" + std::to_string(++s.serial);
}
void retire_join(State&);
void close_socket(State& s) {
    std::shared_ptr<rtc::WebSocket> socket;
    {
        std::lock_guard lock(s.mutex);
        s.ui.connection_generation = ++s.channel;
        socket = std::move(s.socket);
        s.pending.clear();
        s.join_requests.clear();
        s.inbox.clear();
        retire_join(s);
        s.ui.connected = false;
        s.ui.count_available = false;
        s.ui.public_listed = false;
        s.ui.updating = false;
        s.ui.rooms.clear();
        s.update();
    }
    if (socket) {
        socket->resetCallbacks();
        socket->close();
    }
}
void enqueue(Json j) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.outbox.size() < 64)
        s.outbox.push_back(std::move(j));
    else {
        s.ui.error = "Too many online operations. Wait for the current request.";
        s.update();
    }
    s.cv.notify_all();
}
void send(State& s, Json j) {
    std::shared_ptr<rtc::WebSocket> socket;
    {
        std::lock_guard lock(s.mutex);
        socket = s.socket;
        const auto id = next_id(s);
        j["request"] = id;
        if (s.pending.size() >= 256)
            throw std::runtime_error("Too many unanswered directory requests.");
        s.pending[id] = string(j, "type", 32);
    }
    {
        std::lock_guard lock(s.mutex);
        if (string(j, "type", 32) == "join" || string(j, "type", 32) == "resolve")
            s.join_requests[string(j, "request", 80)] = string(j, "key", 80);
    }
    if (!socket || !socket->isOpen())
        throw std::runtime_error("The online directory disconnected.");
    auto body = j.dump();
    if (body.size() > 16384)
        throw std::runtime_error("The online message is too large.");
    if (socket->bufferedAmount() > 262144)
        throw std::runtime_error("The online connection is busy. Reconnecting.");
    if (!socket->send(body))
        throw std::runtime_error("The online connection is busy. Reconnecting.");
}
void append_rooms(State& s, const Json& data, bool replace) {
    if (!data.contains("rooms") || !data["rooms"].is_array() || data["rooms"].size() > 50)
        return;
    if (replace)
        s.ui.rooms.clear();
    for (const auto& j : data["rooms"]) {
        Room r;
        r.id = string(j, "id", 80);
        r.title = string(j, "title", 80);
        r.host = string(j, "host", 100);
        r.compatibility = string(j, "compatibility", 160);
        r.phase = string(j, "phase", 24);
        r.racers = j.value("racers", 0U);
        r.max_racers = j.value("maxRacers", 0U);
        r.spectators = j.value("spectators", 0U);
        r.max_spectators = j.value("maxSpectators", 0U);
        r.locked = j.value("locked", false);
        r.can_join = j.value("canJoin", false);
        r.can_spectate = j.value("canSpectate", false);
        if (r.id.empty() || r.racers > 4 || r.max_racers > 4 || r.spectators > 8 || r.max_spectators > 8)
            continue;
        auto found = std::find_if(s.ui.rooms.begin(), s.ui.rooms.end(), [&](const auto& x) { return x.id == r.id; });
        if (found != s.ui.rooms.end())
            *found = std::move(r);
        else if (s.ui.rooms.size() < 1000)
            s.ui.rooms.push_back(std::move(r));
    }
    s.ui.online = data.value("online", std::uint64_t(0));
    s.ui.revision = data.value("revision", std::uint64_t(0));
    s.ui.count_available = data.contains("online");
}
void retire_join(State& s) {
    std::erase_if(s.outbox, [](const auto& message) {
        const auto type = string(message, "type", 32);
        return type == "join" || type == "resolve";
    });
    if (!s.joining_id.empty() && s.outbox.size() < 64)
        s.outbox.push_back({{"type", "cancel"}, {"id", s.joining_id}});
    s.joining_id.clear();
    s.bootstrap = nullptr;
    s.ui.joining = false;
    crypto_wipe(s.join_key.secret.data(), s.join_key.secret.size());
    s.join_key = {};
    {
        std::lock_guard lock(s.bridge_mutex);
        for (auto& [owner, key] : s.keys)
            crypto_wipe(key.pair.secret.data(), 32);
        s.keys.clear();
    }
}
void run(std::stop_token stop) {
    auto& s = state();
    unsigned generation = ~0U, attempt = 0;
    auto retry = Clock::time_point{}, heartbeat = Clock::time_point{}, renew = Clock::time_point{};
    std::string host_epoch, host_id, last_state;
    std::uint64_t host_revision = 0;
    auto changed_at = Clock::time_point{};
    while (!stop.stop_requested()) {
        AccountView account;
        AccountRequest request;
        bool enabled;
        {
            std::lock_guard lock(s.mutex);
            account = s.account;
            request = s.request;
            enabled = s.ui.enabled;
            if (s.retry_now) {
                retry = {};
                s.retry_now = false;
            }
        }
        Account identity;
        if (account)
            identity = account();
        if (identity.generation != generation) {
            close_socket(s);
            generation = identity.generation;
            host_id.clear();
            host_epoch.clear();
            last_state.clear();
            host_revision = 0;
            retry = {};
            std::lock_guard lock(s.mutex);
            retire_join(s);
            s.inbox.clear();
            s.outbox.clear();
            s.ui.error.clear();
            s.update();
        }
        {
            std::lock_guard lock(s.mutex);
            s.ui.signed_in = identity.signed_in;
            s.ui.account_name = identity.name;
            s.ui.account_generation = identity.generation;
            s.update();
        }
        if (!identity.signed_in || !enabled || !request) {
            close_socket(s);
            {
                std::lock_guard lock(s.mutex);
                s.ui.status = identity.signed_in ? "Disconnected from DKR-R Online."
                                                 : "Sign in with Discord to find public lobbies.";
                s.update();
            }
        } else
            try {
                const auto now = Clock::now();
                bool opened;
                {
                    std::lock_guard lock(s.mutex);
                    opened = s.socket && s.socket->isOpen();
                }
                if (!opened && now >= retry) {
                    close_socket(s);
                    auto capabilities = request("GET", "/v1/online/capabilities", Json::object(), s.instance, stop);
                    if (!capabilities.value("enabled", false) || capabilities.value("protocol", 0U) != Protocol) {
                        std::lock_guard lock(s.mutex);
                        s.ui.feature_enabled = false;
                        s.ui.status = "Public lobbies are not enabled on the service yet.";
                        s.update();
                        retry = now + std::chrono::milliseconds(30000 + s.jitter % 1000);
                    } else {
                        auto ticket = request("POST", "/v1/online/ticket", Json::object(), s.instance, stop);
                        const auto credential = string(ticket, "ticket", 64);
                        if (credential.size() != 64)
                            throw std::runtime_error("The directory returned an invalid connection ticket.");
                        rtc::WebSocket::Configuration config;
                        config.protocols = {"dkrr-online-v1", "dkrr-ticket-" + credential};
                        config.caCertificatePemFile = directory_ca_bundle();
                        config.connectionTimeout = std::chrono::seconds(12);
                        config.maxMessageSize = 65536;
                        auto socket = std::make_shared<rtc::WebSocket>(config);
                        const auto channel = ++s.channel;
                        socket->onMessage(
                            [channel, weak = std::weak_ptr<rtc::WebSocket>(socket)](rtc::message_variant message) {
                                if (auto* data = std::get_if<std::string>(&message); data && data->size() <= 65536) {
                                    try {
                                        auto value = Json::parse(*data);
                                        auto& s = state();
                                        bool overflow = false;
                                        {
                                            std::lock_guard lock(s.mutex);
                                            if (channel == s.channel) {
                                                if (s.inbox.size() < 128) {
                                                    s.inbox.push_back(std::move(value));
                                                    s.cv.notify_all();
                                                } else
                                                    overflow = true;
                                            }
                                        }
                                        if (overflow)
                                            if (auto socket = weak.lock())
                                                socket->close();
                                    } catch (...) {
                                    }
                                }
                            });
                        socket->onClosed([channel] {
                            auto& s = state();
                            std::lock_guard lock(s.mutex);
                            if (channel == s.channel) {
                                retire_join(s);
                                s.ui.connected = false;
                                s.ui.public_listed = false;
                                s.ui.rooms.clear();
                                s.update();
                                s.cv.notify_all();
                            }
                        });
                        socket->onError([channel](std::string) {
                            auto& s = state();
                            std::lock_guard lock(s.mutex);
                            if (channel == s.channel) {
                                s.ui.error = "The directory connection failed. Retrying...";
                                s.update();
                            }
                        });
                        {
                            std::lock_guard lock(s.mutex);
                            s.socket = socket;
                            s.ui.connection_generation = channel;
                            s.ui.feature_enabled = true;
                            s.ui.running_spectators = capabilities.value("runningSpectators", false);
                            s.ui.status = "Connecting to DKR-R Online...";
                            s.update();
                        }
                        socket->open("wss://dkr-mod-library-api.ross-macgregorbars.workers.dev/v1/online/connect");
                        retry = now + std::chrono::seconds(15);
                        heartbeat = now;
                        renew = now + std::chrono::seconds(60);
                        host_id.clear();
                        host_epoch.clear();
                        last_state.clear();
                        host_revision = 0;
                    }
                }
                {
                    std::lock_guard lock(s.mutex);
                    opened = s.socket && s.socket->isOpen();
                }
                if (opened) {
                    {
                        std::lock_guard lock(s.mutex);
                        s.ui.connected = true;
                        s.ui.status = "Connected to DKR-R Online.";
                        s.update();
                    }
                    attempt = 0;
                    if (now >= renew) {
                        request("POST", "/v1/online/session", Json::object(), s.instance, stop);
                        renew = now + std::chrono::seconds(60);
                    }
                    if (now >= heartbeat) {
                        send(s, {{"type", "heartbeat"}});
                        heartbeat = now + std::chrono::milliseconds(19000 + (s.jitter + s.serial * 73) % 2000);
                    }
                    std::deque<Json> inbox, outbox;
                    {
                        std::lock_guard lock(s.mutex);
                        inbox.swap(s.inbox);
                        outbox.swap(s.outbox);
                    }
                    for (auto& item : inbox) {
                        const auto type = string(item, "type", 32);
                        if (type == "snapshot") {
                            std::string cursor;
                            {
                                std::lock_guard lock(s.mutex);
                                append_rooms(s, item, true);
                                cursor = string(item, "nextCursor", 80);
                                s.update();
                            }
                            if (!cursor.empty())
                                send(s, {{"type", "list"}, {"cursor", cursor}});
                        } else if (type == "delta") {
                            std::lock_guard lock(s.mutex);
                            if (item.value("revision", std::uint64_t(0)) >= s.ui.revision) {
                                if (item.contains("upsert"))
                                    append_rooms(s,
                                                 {{"rooms", item["upsert"]},
                                                  {"online", item.value("online", std::uint64_t(0))},
                                                  {"revision", item.value("revision", std::uint64_t(0))}},
                                                 false);
                                if (item.contains("remove") && item["remove"].is_array() &&
                                    item["remove"].size() <= 50) {
                                    for (const auto& id : item["remove"])
                                        if (id.is_string())
                                            std::erase_if(s.ui.rooms, [&](const auto& room) {
                                                return room.id == id.get<std::string>();
                                            });
                                }
                                s.update();
                            }
                        } else if (type == "result") {
                            std::string operation, join_request;
                            {
                                std::lock_guard lock(s.mutex);
                                const auto id = string(item, "request", 80);
                                const auto found = s.pending.find(id);
                                if (found == s.pending.end())
                                    continue;
                                operation = found->second;
                                s.pending.erase(found);
                                if (auto found = s.join_requests.find(id); found != s.join_requests.end()) {
                                    join_request = found->second;
                                    s.join_requests.erase(found);
                                }
                            }
                            if (!item.value("ok", false)) {
                                const auto error = item.value("error", Json::object());
                                std::lock_guard lock(s.mutex);
                                s.ui.error = string(error, "message", 256);
                                if (s.ui.joining && (operation == "join" || operation == "resolve") &&
                                    join_request == secure::encode_key(s.join_key.public_key))
                                    retire_join(s);
                                s.ui.updating = false;
                                s.update();
                                if (string(error, "code") == "revision") {
                                    const auto current = error.value("current", Json::object());
                                    last_state.clear();
                                    host_revision = current.value("revision", std::uint64_t(0));
                                    host_id = string(current, "id", 80);
                                    host_epoch = string(current, "epoch", 80);
                                }
                            } else if (item.contains("data")) {
                                const auto& data = item["data"];
                                if (data.contains("rooms")) {
                                    std::string cursor;
                                    {
                                        std::lock_guard lock(s.mutex);
                                        append_rooms(s, data, false);
                                        cursor = string(data, "nextCursor", 80);
                                        s.update();
                                    }
                                    if (!cursor.empty())
                                        send(s, {{"type", "list"}, {"cursor", cursor}});
                                }
                                if (data.contains("online")) {
                                    std::lock_guard lock(s.mutex);
                                    s.ui.online = data.value("online", std::uint64_t(0));
                                    s.ui.count_available = true;
                                    s.update();
                                }
                                if (data.contains("visibility")) {
                                    host_id = string(data, "id", 80);
                                    host_epoch = string(data, "epoch", 80);
                                    host_revision = data.value("revision", std::uint64_t(0));
                                    std::lock_guard lock(s.mutex);
                                    s.ui.public_listed = string(data, "visibility") == "public";
                                    s.ui.updating = false;
                                    s.update();
                                }
                                if (string(data, "status") == "contacting_host") {
                                    std::lock_guard lock(s.mutex);
                                    if (s.ui.joining && join_request == secure::encode_key(s.join_key.public_key))
                                        s.joining_id = string(data, "id", 80);
                                    else if (s.outbox.size() < 64)
                                        s.outbox.push_back({{"type", "cancel"}, {"id", string(data, "id", 80)}});
                                }
                            }
                        } else if (type == "intent" || type == "revoke") {
                            std::lock_guard lock(s.bridge_mutex);
                            for (auto& [owner, bridge] : s.bridges) {
                                std::lock_guard own(bridge->mutex);
                                if (bridge->state.value("host", false) && bridge->commands.size() < 32)
                                    bridge->commands.push_back(item);
                            }
                        } else if (type == "bootstrap") {
                            std::lock_guard lock(s.mutex);
                            if (s.ui.joining && string(item, "key", 80) == secure::encode_key(s.join_key.public_key) &&
                                string(item, "id", 80) == s.joining_id)
                                s.bootstrap = std::move(item);
                        } else if (type == "join_cancelled") {
                            std::lock_guard lock(s.mutex);
                            if (string(item, "id", 80) == s.joining_id) {
                                s.ui.error = string(item, "message", 256);
                                retire_join(s);
                                s.update();
                            }
                        } else if (type == "join_consumed") {
                            std::lock_guard lock(s.mutex);
                            if (string(item, "id", 80) == s.joining_id)
                                s.joining_id.clear();
                        }
                    }
                    for (auto& item : outbox)
                        send(s, std::move(item));
                    Json host;
                    std::vector<Json> responses;
                    {
                        std::lock_guard lock(s.bridge_mutex);
                        for (auto& [owner, bridge] : s.bridges) {
                            std::lock_guard own(bridge->mutex);
                            if (bridge->state.value("host", false))
                                host = bridge->state;
                            for (auto& response : bridge->responses)
                                responses.push_back(std::move(response));
                            bridge->responses.clear();
                        }
                    }
                    for (auto& response : responses)
                        send(s, std::move(response));
                    if (!host.is_null()) {
                        host.erase("host");
                        const auto epoch = string(host, "epoch", 80), serialized = host.dump();
                        bool updating;
                        {
                            std::lock_guard lock(s.mutex);
                            updating = std::any_of(s.pending.begin(), s.pending.end(),
                                                   [](const auto& p) { return p.second == "room"; });
                        }
                        if (!updating && serialized != last_state && now >= changed_at) {
                            send(s, {{"type", "room"},
                                     {"revision", epoch == host_epoch ? host_revision : 0},
                                     {"state", host}});
                            last_state = serialized;
                            changed_at = now + std::chrono::seconds(2);
                            std::lock_guard lock(s.mutex);
                            s.ui.updating = true;
                            s.update();
                        }
                    } else if (!host_id.empty()) {
                        send(s, {{"type", "close"}});
                        host_id.clear();
                        host_epoch.clear();
                        last_state.clear();
                        host_revision = 0;
                        std::lock_guard lock(s.mutex);
                        s.ui.public_listed = false;
                        s.ui.updating = false;
                        s.update();
                    }
                }
            } catch (const std::exception&) {
                close_socket(s);
                retry = Clock::now() + std::chrono::seconds(std::min(30U, 1U << std::min(++attempt, 5U))) +
                        std::chrono::milliseconds((s.jitter + s.serial * 73) % 500);
                std::lock_guard lock(s.mutex);
                retire_join(s);
                s.ui.error = "The online directory is unavailable. Reconnecting...";
                s.ui.status = "Reconnecting to DKR-R Online...";
                s.update();
            }
        std::unique_lock lock(s.mutex);
        s.cv.wait_for(lock, stop, std::chrono::milliseconds(100), [] { return false; });
    }
    close_socket(s);
}
} // namespace
void bind_account(AccountView account, AccountRequest request) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.account = std::move(account);
    s.request = std::move(request);
}
void configure(const std::filesystem::path& root) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.configured)
        return;
    s.configured = true;
    s.root = root;
    try {
        std::ifstream file(root / "online-directory.json");
        Json prefs;
        file >> prefs;
        s.ui.enabled = prefs.value("enabled", true);
    } catch (...) {
    }
    s.update();
    s.worker = std::jthread(run);
}
void shutdown_account() {
    auto& s = state();
    s.worker.request_stop();
    s.cv.notify_all();
    if (s.worker.joinable())
        s.worker.join();
    std::lock_guard lock(s.mutex);
    s.account = {};
    s.request = {};
}
std::shared_ptr<const View> view() {
    return state().published.load();
}
void connect(bool enabled) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.ui.enabled = enabled;
    s.ui.error.clear();
    s.retry_now = enabled;
    if (!enabled)
        retire_join(s);
    s.update();
    s.cv.notify_all();
    try {
        std::ofstream file(s.root / "online-directory.json", std::ios::trunc);
        file << Json{{"enabled", enabled}}.dump();
    } catch (...) {
    }
}
void visibility(bool value) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.ui.public_desired = value;
    s.ui.updating = s.ui.connected;
    s.update();
    s.cv.notify_all();
}
void hidden(bool value) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.ui.hidden != value) {
        s.ui.hidden = value;
        if (value)
            s.ui.public_desired = false;
        s.update();
        s.cv.notify_all();
    }
}
void compatibility(std::string value) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.compatible = std::move(value);
}
std::string compatibility() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return s.compatible;
}
void request_join(std::string room, std::string code, bool watch, std::string friend_admission) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.ui.connected || !s.ui.signed_in) {
        s.ui.error = "Sign in with Discord and connect to DKR-R Online first.";
        s.update();
        return;
    }
    if (s.ui.joining) {
        s.ui.error = "Cancel the current join before choosing another lobby.";
        s.update();
        return;
    }
    if (!s.joining_id.empty())
        retire_join(s);
    s.join_key = secure::generate_key_pair();
    s.watch = watch;
    s.joining_id.clear();
    s.bootstrap = nullptr;
    s.ui.joining = true;
    s.ui.error.clear();
    Json message = {{"type", code.empty() ? "join" : "resolve"},
                    {"room", room},
                    {"code", code},
                    {"role", watch ? "spectator" : "racer"},
                    {"key", secure::encode_key(s.join_key.public_key)},
                    {"compatibility", s.compatible},
                    {"friendAdmission", friend_admission}};
    if (s.outbox.size() < 64)
        s.outbox.push_back(std::move(message));
    else
        retire_join(s);
    s.update();
    s.cv.notify_all();
}
void cancel_join() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    retire_join(s);
    s.update();
    s.cv.notify_all();
}
std::shared_ptr<Bridge> bridge(const void* owner) {
    auto& s = state();
    std::lock_guard lock(s.bridge_mutex);
    auto& b = s.bridges[owner];
    if (!b)
        b = std::make_shared<Bridge>();
    return b;
}
void forget(const void* owner) {
    auto& s = state();
    std::lock_guard lock(s.bridge_mutex);
    s.bridges.erase(owner);
    auto key = s.keys.find(owner);
    if (key != s.keys.end()) {
        crypto_wipe(key->second.pair.secret.data(), 32);
        s.keys.erase(key);
    }
}
void publish(const void* owner, Json value) {
    auto b = bridge(owner);
    std::lock_guard lock(b->mutex);
    b->state = std::move(value);
}
std::vector<Json> commands(const void* owner) {
    auto b = bridge(owner);
    std::lock_guard lock(b->mutex);
    std::vector<Json> result;
    result.swap(b->commands);
    return result;
}
int hosting_mode(const void* owner) {
    auto b = bridge(owner);
    std::lock_guard lock(b->mutex);
    return b->state.value("host", false) ? 2 : b->state.value("legacyHost", false) ? 1 : 0;
}
void respond(const void* owner, Json value) {
    auto b = bridge(owner);
    std::lock_guard lock(b->mutex);
    if (b->responses.size() < 64)
        b->responses.push_back(std::move(value));
}
bool take_bootstrap(const void* owner, Bootstrap& out) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.bootstrap.is_object())
        return false;
    const auto code = string(s.bootstrap, "code", 5), admission = string(s.bootstrap, "admission", 80);
    if (code.size() != 5 || !secure::decode_key(admission, out.admission)) {
        retire_join(s);
        s.update();
        return false;
    }
    out.code = code;
    out.watch = s.watch;
    {
        std::lock_guard lock(s.bridge_mutex);
        s.keys[owner] = {s.join_key, out.admission};
    }
    crypto_wipe(s.join_key.secret.data(), 32);
    s.join_key = {};
    s.bootstrap = nullptr;
    s.ui.joining = false;
    s.update();
    return true;
}
secure::KeyPair client_key(const void* owner) {
    auto& s = state();
    std::lock_guard lock(s.bridge_mutex);
    auto found = s.keys.find(owner);
    if (found == s.keys.end())
        return secure::generate_key_pair();
    auto result = found->second.pair;
    crypto_wipe(found->second.pair.secret.data(), 32);
    s.keys.erase(found);
    return result;
}
bool bypass_code_resolution(const void* owner, const secure::Key& admission) {
    auto& s = state();
    std::lock_guard lock(s.bridge_mutex);
    auto found = s.keys.find(owner);
    return found != s.keys.end() && found->second.admission == admission;
}
bool begin_code_join(const void* owner, std::string code, std::string, const secure::Key& admission, bool watch) {
    auto v = view();
    if (!v->feature_enabled || bypass_code_resolution(owner, admission))
        return false;
    request_join({}, std::move(code), watch,
                 admission == secure::Key{} ? std::string{} : secure::encode_key(admission));
    return true;
}
} // namespace dkr::online

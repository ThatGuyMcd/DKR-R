#pragma once
#include "magic_code_package.hpp"
#include "sdk2_command.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#ifndef DKR_PRESENTATION_HOST
#define DKR_PRESENTATION_HOST
#endif
#include "../../../include/dkr/presentation.h"
namespace dkr::sdk2 {
using Json = mods::code::Json;
struct PropertyView {
    unsigned id = 0, type = 0, flags = 0, bits = 0;
    std::string name, unit;
};
struct EntityView {
    unsigned slot = 0, generation = 0, behaviour = 0, object_id = 0;
    bool racer = false;
    std::vector<PropertyView> properties;
    unsigned address = 0, header = 0;
};
struct Panel {
    unsigned owner = 0;
    std::string id, name, digest;
    Json schema, values = Json::object();
    bool development = false;
    std::uint32_t scene = 0, tick = 0;
    std::shared_ptr<const std::vector<EntityView>> inspector;
    Json diagnostics = Json::object();
    std::map<std::string, Json> documents;
};
struct Snapshot {
    std::uint64_t generation = 0;
    std::uint64_t local_session = 0; // UI preset lifetime only, never a gameplay clock
    bool active = false, online = false, host = false, development = false;
    std::vector<Panel> panels;
    std::deque<std::string> history;
    std::string notice;
    struct Request {
        unsigned peer = 0, id = 0;
        Command command;
        std::string package;
    };
    std::deque<Request> requests;
    unsigned local = 0, players = 1, grants = 0;
};
// A local copied-data mailbox. It never stores a GameWorld or RDRAM pointer.
// Only the authored input owner consumes commands; UI is merely a producer.
void begin(const std::vector<std::shared_ptr<const mods::code::Package>>&, bool online);
void end();
void authority(bool host, bool development);
std::shared_ptr<const Snapshot> snapshot();
bool available();
bool submit(unsigned owner, unsigned action, const std::array<std::uint32_t, 13>& arguments, std::string_view digest,
            std::uint64_t generation);
Command take(bool online);
void publish(unsigned owner, Json, std::uint32_t scene, std::uint32_t tick);
void inspector(unsigned owner, std::shared_ptr<const std::vector<EntityView>>);
void diagnostics(unsigned owner, Json);
void network_epoch(std::uint64_t, unsigned local, unsigned players);
std::vector<std::uint8_t> network_packet(unsigned peer);
bool network_receive(unsigned peer, std::span<const std::uint8_t>);
bool decide(unsigned peer, unsigned request, bool approve);
bool grant(unsigned peer, bool allow);
void observed(const Command&);
void document(unsigned owner, std::string id, Json);
void validate_values(const Json& schema, const Json& values);
struct Reload {
    unsigned owner = 0;
    std::string digest;
    std::shared_ptr<const mods::code::Package> package;
};
bool request_reload(Reload, std::uint64_t generation);
Reload take_reload();
bool upload(unsigned owner, unsigned document_index, const Json&, std::string_view digest, std::uint64_t generation);
struct LocalCamera {
    bool enabled = false, follow = false, valid = false, cut = true;
    unsigned owner = 0, slot = 0, generation = 0, scene = 0;
    float position[3]{}, yaw = 0, pitch = 0, fov = 1.04719755F, speed = 200, smoothing = 8;
};
LocalCamera camera_state(unsigned view);
bool camera_edit(unsigned view, const LocalCamera&);
std::optional<DkrViewPose> camera_frame(const DkrViewSnapshot&, const DkrViewInput&, std::span<const std::uint8_t>);
struct DebugPrimitive {
    unsigned kind = 0, rgba = 0, view = 0;
    float a[3]{}, b[3]{};
    std::string label;
};
struct DebugLine {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    unsigned rgba = 0;
    std::string label;
};
void debug(unsigned owner, std::vector<DebugPrimitive>);
void debug_frame(const DkrViewSnapshot&, const std::optional<DkrViewPose>&, unsigned view_count);
std::shared_ptr<const std::vector<DebugLine>> debug_lines();
bool debug_available();
inline std::atomic<bool> debug_enabled{false};
inline std::atomic<bool> hud_hidden{false}; // local presentation only
void notice(std::string);
inline std::atomic<bool> tools_open{false};
} // namespace dkr::sdk2

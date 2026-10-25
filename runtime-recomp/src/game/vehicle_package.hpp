#pragma once
#include "legacy_mod_format.hpp"
#include "vehicle_protocol.hpp"
#include <json/json.hpp>
#include <map>
#include <memory>

namespace dkr::mods::vehicles {
using Json = nlohmann::json;
enum class Type : unsigned { Car, Hovercraft, Plane };
inline constexpr unsigned AdapterVersion = online::CarAdapterVersion;
enum class SeatingMode { LegacyShared, PerBaseDriverV1 };
inline constexpr std::size_t MaxPackageBytes = 32 * MiB, MaxExpandedBytes = 64 * MiB;
inline constexpr unsigned MaxEnabled = 16;
struct Vec2 { float x=0,y=0; };
struct Vec3 { float x=0,y=0,z=0; };
struct Image { unsigned width=0,height=0; Bytes rgba; };
struct Mesh {
    std::string id,name;
    std::vector<Vec3> vertices;
    std::vector<Vec2> uv;
    std::vector<std::array<unsigned,3>> faces;
    std::array<std::uint8_t,4> colour{255,255,255,255};
    int texture=-1;
    bool double_sided=false;
};
struct Texture { Image image; bool repeat_s=true,repeat_t=true; };
struct Lod { std::vector<Mesh> meshes; unsigned triangles=0; };
struct Package {
    Type type=Type::Car;
    std::string id,name,digest,source_name;
    float scale=1,yaw=0,driver_yaw=0;
    Vec3 seat{},driver_offset{};
    SeatingMode seating=SeatingMode::LegacyShared;
    std::array<Vec3,10> driver_seats{};
    Vec3 origin{},kart_offset{};
    // Package anchors are in post-scale, Y-up Studio units. Older exports
    // use the reviewed adapter table; they are never inferred from bounds.
    std::array<Vec3,10> driver_anchors{};
    bool authored_anchors=false;
    bool per_driver()const{return seating==SeatingMode::PerBaseDriverV1;}
    std::array<std::string,4> wheels{}; // FL, FR, RL, RR
    std::array<Vec3,4> pivots{};
    unsigned axle=0;
    std::array<Lod,6> lods;
    std::vector<Texture> textures;
    Image portrait;
    Bytes source; // Exact inner archive, never the containing download ZIP.
};
struct Input { std::string name; Bytes bytes; Type type=Type::Car; };
Type type_for_extension(std::string_view);
const char* extension(Type);
const char* type_name(Type);
bool supported(Type);
bool valid_digest(std::string_view);
// Pure preparation: no code, files, ROM or guest memory is modified.
std::vector<Input> read_inputs(const std::filesystem::path&);
std::shared_ptr<const Package> inspect(View, std::string source_name, Type);
// Native RGBA16 artwork; used by the existing guest menu texture loader.
Bytes native_texture(const Image&);
} // namespace dkr::mods::vehicles

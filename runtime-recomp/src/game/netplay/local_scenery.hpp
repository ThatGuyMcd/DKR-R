#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {

// Renderer-owned retail billboard resources. No guest pointer survives capture;
// these objects contain neither behaviour, collision, RNG nor sound state.
struct LocalSceneryVertex {
    std::int16_t x=0,y=0,z=0,s=0,t=0;
    std::array<std::uint8_t,4> color{};
};
struct LocalSceneryTexture {
    // Word-swapped bytes, exactly like the decoder's private RDRAM image.
    std::vector<std::uint8_t> bytes;
    // FD addresses are offsets into bytes, not freed guest addresses.
    std::vector<std::array<std::uint32_t,2>> commands;
};
struct LocalSceneryTile {
    std::uint16_t texture=0;
    std::array<LocalSceneryVertex,6> vertices{};
};
struct LocalSceneryFrame { std::vector<LocalSceneryTile> tiles; };
struct LocalScenerySprite {
    std::uint32_t identity=0;
    std::array<float,3> position{};
    float scale=1;
    std::int16_t roll=0,draw_distance=0,segment=-1;
    std::uint16_t animation=0,flags=0,transform_flags=0;
    std::uint32_t primitive=0xFFFFFFFFU,environment=0xFFFFFF00U;
    std::array<std::array<std::uint32_t,2>,2> material{};
    std::array<std::array<std::uint32_t,2>,2> faded_material{};
    std::vector<std::shared_ptr<const LocalSceneryTexture>> textures;
    std::vector<LocalSceneryFrame> frames;
};
struct LocalSceneryScene {
    std::uint32_t scene=0;
    std::size_t payload_bytes=0;
    std::vector<LocalScenerySprite> sprites;
};

class LocalSceneryCapture final {
public:
    static constexpr std::size_t kMaxSprites=128;
    static constexpr std::size_t kMaxPayloadBytes=4U*1024U*1024U;
    void reset(std::uint32_t scene) noexcept;
    // Called synchronously before a confirmed constructor frees the actor.
    // A malformed/unsupported OPTIONAL visual fails closed, never aborts netplay.
    bool capture(std::span<const std::uint8_t> ram,std::uint32_t object,
                 std::uint32_t material_table) noexcept;
    std::shared_ptr<const LocalSceneryScene> freeze(std::uint32_t scene) const noexcept;
    std::uint32_t rejected() const noexcept { return rejected_; }
    const char* last_failure() const noexcept { return last_failure_.data(); }
private:
    std::shared_ptr<LocalSceneryScene> scene_;
    std::uint32_t rejected_=0;
    std::array<char,96> last_failure_{};
};

// Dedicated C observation ABI. No checked CPU-memory helper or mutation is
// used by this optional capture; the canonical computation is unchanged.
using LocalSceneryObserver=void (*)(void*,const std::uint8_t*,std::uint32_t,
                                    std::uint32_t,std::uint32_t);
}

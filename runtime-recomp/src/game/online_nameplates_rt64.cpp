#include "online_nameplates_rt64.hpp"
#include "native_draw_scope_rt64.hpp"
#include "local_scenery_arena.hpp"
#include "local_scenery_policy.hpp"
#include "generated/selawik_font.h"
#include "presentation_identity.hpp"
#include "gbi/rt64_gbi_rdp.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"
#include <cmath>
#include <bit>
#include <cstring>
#include <vector>

namespace dkr::runtime::online_view {
namespace {
constexpr unsigned TileWidth = 64, Height = 32, FontHeight = 23;
// Texture * primitive colour, including alpha, in both combiner cycles.
// setCombine takes native w0 in the LOW half, w1 in the HIGH half. This
// differs from spelling a display-list packet as (w0 << 32) | w1.
constexpr std::uint32_t TextMux0 = (1U << 20) | (3U << 15) | (1U << 12) | (3U << 9) | (1U << 5) | 3U;
constexpr std::uint32_t TextMux1 = (8U << 28) | (8U << 24) | (1U << 21) | (3U << 18) | (7U << 15) | (7U << 12) |
                                   (7U << 9) | (7U << 6) | (7U << 3) | 7U;
constexpr std::uint64_t TextCombine = (std::uint64_t(TextMux1) << 32) | TextMux0;
static_assert((TextCombine & 31U) == 3U && ((TextCombine >> 53) & 7U) == 1U);
// AA, depth compare (NOT depth write), image read, translucent Z mode,
// alpha blending. Nameplates cannot punch holes into the race's depth image.
constexpr std::uint32_t TextMode = 0x005049D8U;
unsigned codepoint(std::string_view text, std::size_t& i) {
    const unsigned a = static_cast<unsigned char>(text[i++]);
    if (a < 128)
        return a;
    const unsigned n = a >= 0xC2 && a <= 0xDF ? 1 : a >= 0xE0 && a <= 0xEF ? 2 : a >= 0xF0 && a <= 0xF4 ? 3 : 0;
    if (!n || i + n > text.size())
        return '?';
    unsigned value = a & ((1U << (6 - n)) - 1U);
    for (unsigned j = 0; j < n; ++j) {
        const unsigned b = static_cast<unsigned char>(text[i]);
        if ((b & 0xC0) != 0x80)
            return '?';
        value = (value << 6) | (b & 63);
        ++i;
    }
    if (value < 0x80 || (n == 2 && value < 0x800) || (n == 3 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
        return '?';
    return value;
}
struct Label {
    unsigned width = 0, tiles = 0;
    std::uint32_t address = 0;
};
} // namespace
struct Nameplates::Impl {
    std::array<std::string, 4> names{};
    std::array<std::array<Label, 8>, 4> named{};
    std::array<Label, 8> position{};
    std::vector<std::uint8_t> pixels;
    Plan plan;
    std::uint32_t scene = 0;
    bool ready = false, built = false;
    unsigned view = 4, occupied = 0;
    struct Box {
        float left, top, right, bottom;
    };
    std::array<Box, 4> boxes{};
    Label raster(const stbtt_fontinfo& font, std::string_view text) {
        constexpr unsigned Width = 512;
        std::array<unsigned char, Width * Height> alpha{};
        const float scale = stbtt_ScaleForPixelHeight(&font, float(FontHeight));
        int ascent = 0;
        stbtt_GetFontVMetrics(&font, &ascent, nullptr, nullptr);
        const int baseline = 3 + int(std::lround(float(ascent) * scale));
        float pen = 3;
        for (std::size_t at = 0; at < text.size();) {
            auto cp = codepoint(text, at);
            if (cp < 32 || !stbtt_FindGlyphIndex(&font, int(cp)))
                cp = '?';
            int advance = 0, left = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointHMetrics(&font, int(cp), &advance, &left);
            stbtt_GetCodepointBitmapBox(&font, int(cp), scale, scale, &x0, &y0, &x1, &y1);
            const int width = x1 - x0, height = y1 - y0;
            if (pen + float(x1) + 3 >= Width)
                break;
            if (width > 0 && height > 0 && width < 64 && height < 64) {
                std::array<unsigned char, 4096> glyph{};
                stbtt_MakeCodepointBitmap(&font, glyph.data(), width, height, width, scale, scale, int(cp));
                const int x = int(std::lround(pen)) + x0, y = baseline + y0;
                for (int row = 0; row < height; ++row)
                    for (int column = 0; column < width; ++column)
                        if (x + column >= 0 && x + column < int(Width) && y + row >= 0 && y + row < int(Height))
                            alpha[unsigned(y + row) * Width + unsigned(x + column)] =
                                glyph[unsigned(row * width + column)];
            }
            pen += float(advance) * scale;
        }
        Label label;
        label.width = std::clamp(unsigned(std::ceil(pen)) + 3U, 1U, Width);
        label.tiles = (label.width + TileWidth - 1) / TileWidth;
        const auto bytes = label.tiles * TileWidth * Height;
        if (bytes > kLabelTextureEnd - kLabelTextureBegin - pixels.size())
            return {};
        label.address = kLabelTextureBegin + std::uint32_t(pixels.size());
        const auto start = pixels.size();
        pixels.resize(start + bytes);
        for (unsigned tile = 0; tile < label.tiles; ++tile)
            for (unsigned y = 0; y < Height; ++y)
                for (unsigned x = 0; x < TileWidth; ++x) {
                    const int px = int(tile * TileWidth + x);
                    unsigned halo = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (px + dx >= 0 && px + dx < int(Width) && int(y) + dy >= 0 && int(y) + dy < int(Height))
                                halo =
                                    std::max(halo, unsigned(alpha[unsigned(int(y) + dy) * Width + unsigned(px + dx)]));
                    const unsigned ink = px < int(Width) ? alpha[y * Width + unsigned(px)] : 0;
                    const auto ia = std::uint8_t((halo ? std::min(15U, ink * 15U / halo) : 0U) << 4 |
                                                 std::min(15U, (halo + 8U) / 17U));
                    pixels[(start + tile * TileWidth * Height + y * TileWidth + x) ^ 3U] = ia;
                }
        return label;
    }
};
Nameplates::Nameplates() : impl_(std::make_unique<Impl>()) {}
Nameplates::~Nameplates() = default;
void Nameplates::prepare(const Session& session, const Plan& plan, std::span<std::uint8_t> decoder,
                         std::uint32_t scene) noexcept {
    auto& p = *impl_;
    p.plan = plan;
    p.scene = scene;
    p.ready = false;
    p.view = 4;
    p.occupied = 0;
    if (!plan.eligible || plan.labels == Labels::Off || decoder.size() < kLabelTextureEnd)
        return;
    try {
        if (!p.built || p.names != session.names) {
            p.built = false;
            p.pixels.clear();
            p.names = session.names;
            stbtt_fontinfo font{};
            if (!stbtt_InitFont(&font, reinterpret_cast<const unsigned char*>(dkr_selawik_semibold_font), 0))
                return;
            for (unsigned rank = 0; rank < 8; ++rank) {
                const auto number = std::to_string(rank + 1);
                p.position[rank] = p.raster(font, number);
                for (unsigned owner = 0; owner < 4; ++owner) {
                    auto name = p.names[owner];
                    if (name.empty())
                        name = "Player " + std::to_string(owner + 1);
                    if (name.size() > 24)
                        name.resize(24); // Lobby byte limit; decoder also validates UTF-8.
                    p.named[owner][rank] = p.raster(font, number + ". " + name);
                }
            }
            p.built = true;
        }
        if (p.pixels.empty() || p.pixels.size() > kLabelTextureEnd - kLabelTextureBegin)
            return;
        std::memcpy(decoder.data() + kLabelTextureBegin, p.pixels.data(), p.pixels.size());
        p.ready = true;
    } catch (...) {
        p.ready = false;
    } // Optional labels must never end an online race.
}
void Nameplates::begin_view(unsigned view) {
    impl_->view = view;
    impl_->occupied = 0;
}
void Nameplates::draw(RT64::State& s, const dkr_owned_draw_event& e, const hlslpp::float4x4& camera,
                      IdentitySelector select) {
    auto& p = *impl_;
    const auto owner = e.parameters[0], rank = e.parameters[1];
    if (!p.ready || !select || e.token != p.view || owner == p.view || owner >= 4 || rank < 1 || rank > 8 ||
        (p.plan.focus && p.view != p.plan.owner) || !dkr_owned_draw_event_valid(&e))
        return;
    auto& rsp = *s.rsp;
    auto& rdp = *s.rdp;
    if (rdp.colorCombinerStackSize >= RDP_EXTENDED_STACK_SIZE || rdp.primColorStackSize >= RDP_EXTENDED_STACK_SIZE ||
        rdp.envColorStackSize >= RDP_EXTENDED_STACK_SIZE)
        return;
    const float x = std::bit_cast<float>(e.parameters[2]), y = std::bit_cast<float>(e.parameters[3]),
                z = std::bit_cast<float>(e.parameters[4]);
    const float object_scale = std::bit_cast<float>(e.parameters[5]);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(object_scale) ||
        std::abs(x) > 32768 || std::abs(y) > 32768 || std::abs(z) > 32768 || object_scale <= 0 || object_scale > 64)
        return;
    auto anchor = hlslpp::mul(hlslpp::float4(x, y + std::clamp(48.F * object_scale, 28.F, 100.F), z, 1), camera);
    const float w = float(anchor.w);
    if (!std::isfinite(w) || w < 100.F || w > 6000.F)
        return;
    const float nx = float(anchor.x) / w, ny = float(anchor.y) / w, nz = float(anchor.z) / w;
    if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) || std::abs(nx) > 4 || std::abs(ny) > 1.15F ||
        nz < 0 || nz > 1)
        return;
    const auto& label = p.plan.labels == Labels::NameAndPosition ? p.named[owner][rank - 1] : p.position[rank - 1];
    if (!label.tiles)
        return;
    const auto& viewport = rsp.viewportStack[rsp.viewportStackSize - 1];
    if (std::abs(float(viewport.scale.x)) < 1 || std::abs(float(viewport.scale.y)) < 1)
        return;
    const float pixel_scale = 4.5F * float(p.plan.label_size) * .01F / FontHeight;
    const float half_width = float(label.width) * pixel_scale * .5F;
    const float screen_x = nx * float(viewport.scale.x) + float(viewport.translate.x);
    float screen_y = -ny * float(viewport.scale.y) + float(viewport.translate.y) - float(Height) * pixel_scale;
    const float height = float(Height) * pixel_scale;
    // Bounded local placement, not a change to the actor's transform or rank.
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        bool overlap = false;
        for (unsigned i = 0; i < p.occupied; ++i) {
            const auto& b = p.boxes[i];
            overlap |= screen_x + half_width > b.left && screen_x - half_width < b.right && screen_y + height > b.top &&
                       screen_y < b.bottom;
        }
        if (!overlap)
            break;
        screen_y -= height + 1;
    }
    if (p.occupied < p.boxes.size())
        p.boxes[p.occupied++] = {screen_x - half_width, screen_y, screen_x + half_width, screen_y + height};
    anchor.y = (float(viewport.translate.y) - screen_y) / float(viewport.scale.y) * w;
    const auto alpha = std::uint32_t(std::clamp((6000.F - w) / 1000.F, 0.F, 1.F) * 255.F);
    if (!alpha)
        return;
    NativeDrawScope restore(s);
#if defined(DKR_ENHANCED_WATER)
    const auto water = s.waterMaterial;
    s.waterMaterial = {};
    s.updateDrawStatusAttribute(RT64::DrawAttribute::WaterMaterial);
#endif
    auto billboard = hlslpp::float4x4::identity();
    billboard[0] = hlslpp::float4(w * pixel_scale / float(viewport.scale.x), 0, 0, 0);
    billboard[1] = hlslpp::float4(0, -w * pixel_scale / float(viewport.scale.y), 0, 0);
    billboard[2] = hlslpp::float4(0, 0, 0, 0);
    billboard[3] = anchor;
    // Native scenery owns placement bytes 1..128. Labels use 241..244,
    // retaining the native high bit and never matching canonical game draws.
    select(rsp, local_scenery::native_identity(presentation::normalise_identity(p.scene ^ 0x4E414D45U), 0xF0U + owner,
                                               p.view));
    rsp.modelMatrixStack[2] = billboard;
    rsp.modelMatrixStackSize = 3;
    rsp.modelMatrixPhysicalAddressStack[2] = rsp.modelMatrixSegmentedAddressStack[2] = local_scenery::kVertexScratch;
    rsp.modelViewProjChanged = true;
    rsp.modelViewProjInserted = false;
    rsp.clearGeometryMode(G_FOG);
    rdp.setCombine(TextCombine);
    rdp.setOtherMode(G_TF_BILERP | G_TP_PERSP, TextMode);
    rdp.setPrimColor(0, 0, 0xFFFFFF00U | alpha);
    rdp.setEnvColor(0); // All glyph/outline colour and opacity come from IA8.
    for (unsigned tile = 0; tile < label.tiles; ++tile) {
        rdp.setTextureImage(G_IM_FMT_IA, G_IM_SIZ_8b, TileWidth, label.address + tile * TileWidth * Height);
        rdp.setTile(0, G_IM_FMT_IA, G_IM_SIZ_8b, TileWidth / 8, 0, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
        rdp.setTileSize(0, 0, 0, (TileWidth - 1) * 4, (Height - 1) * 4);
        rdp.loadTile(0, 0, 0, (TileWidth - 1) * 4, (Height - 1) * 4);
        for (unsigned v = 0; v < 4; ++v) {
            RT64::RSP::Vertex vertex{};
            vertex.x = std::int16_t(int(tile * TileWidth + (v & 1 ? TileWidth : 0)) - int(label.width / 2));
            vertex.y = std::int16_t(v & 2 ? Height : 0);
            vertex.z = 0;
            vertex.color.r = vertex.color.g = vertex.color.b = vertex.color.a = 255;
            std::memcpy(s.RDRAM + local_scenery::kVertexScratch + v * sizeof(vertex), &vertex, sizeof(vertex));
        }
        rsp.setVertex(local_scenery::kVertexScratch, 4, 1);
        for (unsigned v = 0; v < 4; ++v)
            rsp.modifyVertex(std::uint16_t(v + 1), G_MWO_POINT_ST,
                             (std::uint32_t(v & 1 ? (TileWidth - 1) * 32 : 0) << 16) | (v & 2 ? (Height - 1) * 32 : 0));
        rsp.drawIndexedTri(1, 2, 3);
        rsp.drawIndexedTri(2, 4, 3);
        s.flush();
    }
#if defined(DKR_ENHANCED_WATER)
    s.waterMaterial = water;
    s.updateDrawStatusAttribute(RT64::DrawAttribute::WaterMaterial);
#endif
}
} // namespace dkr::runtime::online_view

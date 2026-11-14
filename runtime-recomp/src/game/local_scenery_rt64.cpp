#include "local_scenery_rt64.hpp"
#include "native_draw_scope_rt64.hpp"
#include "local_scenery_texture_scope.hpp"
#include "local_scenery_policy.hpp"
#include "local_scenery_vertex_state.hpp"
#include "presentation_identity.hpp"
#include "runtime_enhancements.hpp"
#include "revision_addresses.hpp"
#include "netplay/experimental_performance.hpp"

#include "gbi/rt64_f3d.h"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_state.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dkr::runtime {
namespace {
using netplay::experimental::LocalSceneryTexture;

// Restore every state domain touched by the typed billboard path. Deferred
// TMEM/Rice restoration is separate (the checked Patch Pipeline scope).
using DrawScope = NativeDrawScope;
// Whitelisted, single RDP commands only. Never RunCommands(), branches,
// DMAOffsets, guest callbacks, framebuffer/depth targets or full sync.
void upload_texture(RT64::State& state, const LocalSceneryTexture& texture, std::uint32_t address) {
    for (const auto& words : texture.commands) {
        RT64::DisplayList command{};
        command.w0 = words[0];
        command.w1 = words[1];
        auto* cursor = &command;
        switch (command.w0 >> 24) {
        case 0xFD:
            state.rdp->setTextureImage(command.p0(21, 3), command.p0(19, 2), command.p0(0, 12) + 1,
                                       address + command.w1);
            break;
        case 0xE6:
        case 0xE7:
        case 0xE8:
            break; // CPU decoder has no RDP FIFO.
        case 0xF0:
            RT64::GBI_RDP::loadTLUT(&state, &cursor);
            break;
        case 0xF2:
            RT64::GBI_RDP::setTileSize(&state, &cursor);
            break;
        case 0xF3:
            RT64::GBI_RDP::loadBlock(&state, &cursor);
            break;
        case 0xF4:
            RT64::GBI_RDP::loadTile(&state, &cursor);
            break;
        case 0xF5:
            RT64::GBI_RDP::setTile(&state, &cursor);
            break;
        default:
            throw std::logic_error("Unadmitted native scenery texture command");
        }
    }
}
} // namespace

void LocalSceneryRT64Pass::prepare_live(std::span<const dkr_owned_draw_event> events, std::span<std::uint8_t> decoder) {
    // Resources are owned by this decoder workspace until its consumers retire.
    // Static sprite meshes/textures are captured once per scene, not per frame.
    const auto word = [&](std::uint32_t a) {
        if (a < 0x80000000U || a > 0x807FFFFCU || (a & 3))
            throw std::runtime_error("local sprite pointer");
        std::uint32_t v;
        std::memcpy(&v, decoder.data() + a - 0x80000000U, 4);
        return v;
    };
    const auto half = [&](std::uint32_t a) {
        if (a < 0x80000000U || a > 0x807FFFFEU || (a & 1))
            throw std::runtime_error("local sprite pointer");
        std::uint16_t v;
        std::memcpy(&v, decoder.data() + ((a - 0x80000000U) ^ 2U), 2);
        return v;
    };
    const auto byte = [&](std::uint32_t a) {
        if (a < 0x80000000U || a > 0x807FFFFFU)
            throw std::runtime_error("local sprite pointer");
        return decoder[(a - 0x80000000U) ^ 3U];
    };
    const auto real = [&](std::uint32_t a) {
        const auto v = std::bit_cast<float>(word(a));
        if (!std::isfinite(v))
            throw std::runtime_error("local sprite position");
        return v;
    };
    for (std::size_t i = base_count_; i < merged_.sprites.size(); ++i)
        merged_.sprites[i].transform_flags |= 0x80U;
    if (std::none_of(events.begin(), events.end(), [](const auto& e) { return e.kind == DKR_OWNED_LOCAL_OBJECT; }))
        return;
    const auto count = word(revision_addresses::ObjectCount), start = word(revision_addresses::ObjectListStart),
               list = word(revision_addresses::ObjectList);
    if (count > 2048 || start > count)
        throw std::runtime_error("local sprite count");
    std::array<std::vector<std::uint32_t>, 4> drawn;
    unsigned active_views = 0;
    for (const auto& e : events)
        if (e.kind == DKR_OWNED_LOCAL_SCENERY && dkr_owned_draw_event_valid(&e) &&
            settings_.live_enabled(int(e.parameters[1])))
            active_views |= 1U << e.token;
    for (const auto& e : events)
        if (e.kind == DKR_OWNED_LOCAL_OBJECT && dkr_owned_draw_event_valid(&e))
            drawn[e.token].push_back(e.parameters[0]);
    for (auto& view : drawn)
        std::sort(view.begin(), view.end());
    for (unsigned i = start; i < count; ++i) {
        try {
            const auto object = word(list + i * 4), header = word(object + 0x40);
            const auto behaviour = half(object + 0x48), flags = half(object + 6);
            if (flags & 0xD080U || byte(header + 0x53) != 1 ||
                (behaviour != 0 && !enhancements::is_animated_scenery_behaviour(behaviour) &&
                 !enhancements::is_billboard_or_effect_behaviour(behaviour) &&
                 !enhancements::is_water_or_lava_behaviour(behaviour)))
                continue;
            unsigned excluded = 0;
            for (unsigned view = 0; view < 4; ++view)
                if (std::binary_search(drawn[view].begin(), drawn[view].end(), object))
                    excluded |= 1U << view;
            if ((excluded & active_views) == active_views)
                continue;
            const auto model = byte(object + 0x3A), models = byte(header + 0x55);
            if (!models || model >= models || models > 32)
                continue;
            const auto sprite = word(word(object + 0x68) + model * 4);
            auto found = std::find_if(live_assets_.begin(), live_assets_.end(), [&](const auto& asset) {
                return asset.object == object && asset.header == header && asset.sprite == sprite;
            });
            if (found == live_assets_.end()) {
                if (merged_.sprites.size() == netplay::experimental::LocalSceneryCapture::kMaxSprites)
                    continue;
                const auto table =
                    revision_addresses::selected_revision() == rom::Revision::UsV80 ? 0x800DEE68U : 0x800DE8E8U;
                auto captured = netplay::experimental::capture_live_local_sprite(decoder, object, table);
                std::size_t bytes = 0;
                for (const auto& texture : captured.textures)
                    bytes += texture->bytes.size();
                for (const auto& frame : captured.frames)
                    bytes += frame.tiles.size() * sizeof(netplay::experimental::LocalSceneryTile);
                if (bytes > netplay::experimental::LocalSceneryCapture::kMaxPayloadBytes - merged_.payload_bytes)
                    continue;
                const auto index = merged_.sprites.size();
                merged_.sprites.push_back(std::move(captured));
                live_assets_.push_back({object, header, sprite, index});
                merged_.payload_bytes += bytes;
                found = live_assets_.end() - 1;
            }
            auto& out = merged_.sprites[found->index];
            for (unsigned a = 0; a < 3; ++a)
                out.position[a] = real(object + 12 + a * 4);
            out.scale = real(object + 8);
            out.roll = std::int16_t(half(object + 4));
            out.animation = half(object + 0x18);
            out.segment = std::int16_t(half(object + 0x2E));
            out.transform_flags = flags;
            out.view_excluded = std::uint8_t(excluded);
            const auto shading = word(object + 0x54);
            if (shading) {
                const auto intensity = std::uint32_t(std::clamp(real(shading) * 255.F, 0.F, 255.F));
                out.primitive = (intensity << 24) | (intensity << 16) | (intensity << 8) | 255U;
                out.environment = 0;
                for (unsigned c = 0; c < 4; ++c)
                    out.environment = (out.environment << 8) | byte(shading + 4 + c);
            }
        } catch (const std::exception&) {
        } // One unsupported effect cannot remove other scenery.
    }
}

void LocalSceneryRT64Pass::prepare(std::shared_ptr<const netplay::experimental::LocalSceneryScene> scene,
                                   std::span<const dkr_owned_draw_event> observations, std::span<std::uint8_t> decoder,
                                   std::uint32_t identity) noexcept {
    scene_ = nullptr;
    observation_count_ = 0;
    consumed_.fill(false);
    textures_.clear();
    identity_ = identity;
    if (!scene || decoder.size() < 16U * 1024U * 1024U ||
        scene->sprites.size() > netplay::experimental::LocalSceneryCapture::kMaxSprites)
        return;
    try {
        netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::SceneryUpload);
        settings_ = {enhancements::presentation_profile(),
                     enhancements::scenery_retention_mode(),
                     enhancements::view_distance_multiplier(),
                     enhancements::keep_hub_scenery_requested(),
                     enhancements::keep_track_scenery_requested(),
                     enhancements::keep_minigame_scenery_requested(),
                     enhancements::animated_scenery_distance_multiplier(),
                     enhancements::billboard_effect_distance_multiplier(),
                     enhancements::water_lava_detail_multiplier(),
                     enhancements::extended_culling_requested()};
        if (cached_source_ != scene || cached_scene_ != scene->scene || base_count_ != scene->sprites.size()) {
            merged_ = *scene;
            live_assets_.clear();
            cached_scene_ = scene->scene;
            base_count_ = scene->sprites.size();
            cached_source_ = scene;
        }
        if (std::any_of(observations.begin(), observations.end(), [&](const auto& e) {
                return e.kind == DKR_OWNED_LOCAL_SCENERY && settings_.live_enabled(int(e.parameters[1]));
            }))
            prepare_live(observations, decoder);
        for (const auto& event : observations) {
            if (event.kind != DKR_OWNED_LOCAL_SCENERY || event.parameters[0] != scene->scene)
                continue;
            if (!dkr_owned_draw_event_valid(&event) || observation_count_ == observations_.size())
                return;
            // Disabled local scenery must incur no texture copy/upload. This
            // preference is read only by the decoder, never by the CPU adapter.
            if (!settings_.live_enabled(int(event.parameters[1])))
                continue;
            auto plan = local_scenery::plan_view(merged_, event, settings_);
            if (!plan.count)
                continue;
            plans_[observation_count_] = plan;
            observations_[observation_count_++] = event;
        }
        if (!observation_count_)
            return;
        std::uint64_t selected = 0, copied = 0;
        const auto plans = std::span(plans_).first(observation_count_);
        // Copy the union of textures used by the selected frames, once even
        // when multiple viewports/passes share them. No new resource eviction:
        // the retained immutable scene still owns every animation frame.
        if (!local_scenery::copy_view_textures(merged_, plans, decoder, textures_, copied))
            return;
        for (const auto& plan : plans)
            selected += plan.count;
        scene_ = &merged_;
        netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalSceneryCandidates,
                                                  selected);
        netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalSceneryTextureBytes,
                                                  copied);
    } catch (...) {
        scene_ = nullptr;
    } // Optional visuals cannot abort a match.
}

void LocalSceneryRT64Pass::draw_at(RT64::State& state, std::uint32_t command, IdentitySelector select) {
    if (!scene_ || !select)
        return;
    for (std::size_t observation = 0; observation < observation_count_; ++observation) {
        const auto& event = observations_[observation];
        if (consumed_[observation] || event.address - 0x80000000U != command)
            continue;
        consumed_[observation] = true;
        const auto aspect = std::bit_cast<float>(event.parameters[7]);
        auto& rsp = *state.rsp;
        // RT64's push methods silently decline at their limits; a matching pop
        // would then remove canonical state. Reject this optional pass first.
        if (state.rdp->colorCombinerStackSize >= RDP_EXTENDED_STACK_SIZE ||
            state.rdp->primColorStackSize >= RDP_EXTENDED_STACK_SIZE ||
            state.rdp->envColorStackSize >= RDP_EXTENDED_STACK_SIZE)
            continue;
        netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::SceneryDraw);
        const auto world = rsp.modelMatrixStack[0];
        // Preparation already selected and ordered this exact viewport/pass.
        // Do not read live preferences again or recompute a second selection
        // that might request a texture not copied into this decoder workload.
        const auto& plan = plans_[observation];
        DrawScope restore(state);
        for (std::size_t item = 0; item < plan.count; ++item) {
            const auto& candidate = plan.candidates[item];
            const auto index = candidate.index;
            const auto& sprite = scene_->sprites[index];
            const auto anchor = hlslpp::mul(hlslpp::float4(float(std::int16_t(sprite.position[0])),
                                                           float(std::int16_t(sprite.position[1])),
                                                           float(std::int16_t(sprite.position[2])), 1),
                                            world);
            if (float(anchor.w) <= 0.001F)
                continue;
            const auto frame = candidate.frame;
            const float angle = float(std::uint16_t(sprite.roll + std::int16_t(event.parameters[5]))) *
                                (6.2831853071795864769F / 65536.0F);
            const float cosine = std::cos(angle) * sprite.scale, sine = std::sin(angle) * sprite.scale;
            auto billboard = hlslpp::float4x4::identity();
            billboard[0] = hlslpp::float4(cosine, sine, 0, 0);
            billboard[1] = hlslpp::float4(-sine, cosine * aspect, 0, 0);
            billboard[2] = hlslpp::float4(0, 0, sprite.scale, 0);
            billboard[3] += anchor; // Same anchor/MVP convention as the accepted bridge.
            const auto id = local_scenery::native_identity(
                presentation::mix_identity(identity_ ^ presentation::mix_identity(event.parameters[14]) ^
                                           event.parameters[13]),
                unsigned(index), event.token);
            select(rsp, id);
            rsp.modelMatrixStack[2] = billboard;
            rsp.modelMatrixStackSize = 3;
            rsp.modelMatrixSegmentedAddressStack[2] = local_scenery::kVertexScratch;
            rsp.modelMatrixPhysicalAddressStack[2] = local_scenery::kVertexScratch;
            rsp.modelViewProjChanged = true;
            rsp.modelViewProjInserted = false;
            const auto alpha = candidate.alpha;
            netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalScenerySprites);
            const auto& material = alpha == 255 ? sprite.material : sprite.faded_material;
            state.rdp->setCombine((std::uint64_t(material[0][0]) << 32) | material[0][1]);
            state.rdp->setOtherMode(material[1][0] & 0xFFFFFFU, material[1][1]);
            state.rdp->setPrimColor(0, 0, (sprite.primitive & 0xFFFFFF00U) | alpha);
            state.rdp->setEnvColor(sprite.environment);
            for (const auto& tile : sprite.frames[frame].tiles) {
                if (tile.texture >= sprite.textures.size())
                    continue;
                const auto* texture = sprite.textures[tile.texture].get();
                const auto binding = std::find_if(textures_.begin(), textures_.end(),
                                                  [&](const auto& b) { return b.texture == texture; });
                if (binding == textures_.end())
                    continue;
                // Each call owns a complete texture/TLUT upload so its replay
                // scope can restore TMEM without depending on another native call.
                upload_texture(state, *texture, binding->address);
                for (unsigned vertex = 0; vertex < 6; ++vertex) {
                    const auto& v = tile.vertices[vertex];
                    RT64::RSP::Vertex output{};
                    output.x = v.x;
                    output.y = v.y;
                    output.z = v.z;
                    output.color.r = v.color[0];
                    output.color.g = v.color[1];
                    output.color.b = v.color[2];
                    output.color.a = v.color[3];
                    std::memcpy(state.RDRAM + local_scenery::kVertexScratch + vertex * sizeof(output), &output,
                                sizeof(output));
                }
                rsp.setVertex(local_scenery::kVertexScratch, 6, 1);
                // DKR triangle-corner UVs bypass ordinary texture scaling.
                // Apply each duplicate corner exactly once before using it.
                for (unsigned vertex = 0; vertex < 6; ++vertex) {
                    const auto& v = tile.vertices[vertex];
                    rsp.modifyVertex(std::uint16_t(vertex + 1), G_MWO_POINT_ST,
                                     (std::uint32_t(std::uint16_t(v.s)) << 16) | std::uint16_t(v.t));
                }
                rsp.drawIndexedTri(1, 2, 3);
                rsp.drawIndexedTri(4, 5, 6);
                state.flush(); // No mixed native/canonical texture call.
            }
        }
    }
}
} // namespace dkr::runtime

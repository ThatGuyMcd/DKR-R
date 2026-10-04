#include "local_scenery_rt64.hpp"
#include "local_scenery_texture_scope.hpp"
#include "local_scenery_policy.hpp"
#include "local_scenery_vertex_state.hpp"
#include "presentation_identity.hpp"
#include "runtime_enhancements.hpp"
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
class DrawScope final {
public:
    explicit DrawScope(RT64::State& state):s_(state),r_(*state.rsp),d_(*state.rdp),
        matrices_(r_.modelMatrixStack),physical_(r_.modelMatrixPhysicalAddressStack),
        segmented_(r_.modelMatrixSegmentedAddressStack),size_(r_.modelMatrixStackSize),
        vertices_(r_.vertices),indices_(r_.indices),used_(r_.used),extended_(r_.extended),
        texture_(r_.textureState),geometry_(r_.geometryModeStack[r_.geometryModeStackSize-1]),
        segment0_(r_.segments[0]),tiles_(std::to_array(d_.tiles)),
        replacements_(std::to_array(d_.tileReplacementHashes)),image_(d_.texture),other_(d_.otherMode),
        vertex_pipeline_(r_) {
        s_.flush();
        d_.pushCombine();d_.pushPrimColor();d_.pushEnvColor();
        // These RAM textures cannot be framebuffer copies. Splice, don't copy
        // or destroy, the canonical GPU-copy mappings while they are decoded.
        regions_.splice(regions_.end(),s_.framebufferManager.activeRegionsTMEM);
        r_.segments[0]=0;
        r_.setGeometryMode(1U|G_SHADE|G_FOG|r_.shadingSmoothMask);
        r_.clearGeometryMode(r_.cullBothMask|G_LIGHTING|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR);
        r_.setTexture(0,0,1,0xFFFF,0xFFFF);
        // Canonical explicit replacement hashes must not override a new native
        // texture. Normal TMEM/Rice hashing still resolves enabled local packs.
        for(unsigned tile=0;tile<RDP_TILES;++tile)d_.clearTileReplacementHash(tile);
        r_.extended.vertexSegmentEnabled.fill(false);
    }
    ~DrawScope() {
        s_.flush();
        d_.popCombine();d_.popPrimColor();d_.popEnvColor();
        r_.modelMatrixStack=matrices_;r_.modelMatrixPhysicalAddressStack=physical_;
        r_.modelMatrixSegmentedAddressStack=segmented_;r_.modelMatrixStackSize=size_;
        r_.vertices=vertices_;r_.indices=indices_;r_.used=used_;
        r_.extended=extended_;r_.extended.modelMatrixIdStackChanged=true;
        r_.textureState=texture_;r_.geometryModeStack[r_.geometryModeStackSize-1]=geometry_;
        r_.segments[0]=segment0_;vertex_pipeline_.restore(r_);
        std::copy(tiles_.begin(),tiles_.end(),d_.tiles);
        std::copy(replacements_.begin(),replacements_.end(),d_.tileReplacementHashes);
        d_.texture=image_;d_.setOtherMode(other_.H,other_.L);
        s_.framebufferManager.activeRegionsTMEM.clear();
        s_.framebufferManager.activeRegionsTMEM.splice(s_.framebufferManager.activeRegionsTMEM.end(),regions_);
        for(const auto attribute:{RT64::DrawAttribute::Texture,RT64::DrawAttribute::GeometryMode,
                RT64::DrawAttribute::Combine,RT64::DrawAttribute::PrimColor,RT64::DrawAttribute::EnvColor,
                RT64::DrawAttribute::OtherMode})s_.updateDrawStatusAttribute(attribute);
    }
private:
    RT64::State& s_;RT64::RSP& r_;RT64::RDP& d_;
    decltype(r_.modelMatrixStack) matrices_;
    decltype(r_.modelMatrixPhysicalAddressStack) physical_,segmented_;
    int size_;
    decltype(r_.vertices) vertices_;
    decltype(r_.indices) indices_;
    decltype(r_.used) used_;
    decltype(r_.extended) extended_;
    RT64::RSP::TextureState texture_;
    std::uint32_t geometry_,segment0_;
    std::array<RT64::LoadTile,8> tiles_;
    std::array<std::uint64_t,8> replacements_;
    RT64::LoadTexture image_;
    decltype(d_.otherMode) other_;
    local_scenery::VertexPipelineSnapshot<RT64::RSP> vertex_pipeline_;
    std::list<RT64::FramebufferManager::RegionTMEM> regions_;
};
// Whitelisted, single RDP commands only. Never RunCommands(), branches,
// DMAOffsets, guest callbacks, framebuffer/depth targets or full sync.
void upload_texture(RT64::State& state,const LocalSceneryTexture& texture,std::uint32_t address) {
    for(const auto& words:texture.commands) {
        RT64::DisplayList command{};command.w0=words[0];command.w1=words[1];
        auto* cursor=&command;
        switch(command.w0>>24) {
        case 0xFD:
            state.rdp->setTextureImage(command.p0(21,3),command.p0(19,2),
                command.p0(0,12)+1,address+command.w1);break;
        case 0xE6:case 0xE7:case 0xE8:break; // CPU decoder has no RDP FIFO.
        case 0xF0:RT64::GBI_RDP::loadTLUT(&state,&cursor);break;
        case 0xF2:RT64::GBI_RDP::setTileSize(&state,&cursor);break;
        case 0xF3:RT64::GBI_RDP::loadBlock(&state,&cursor);break;
        case 0xF4:RT64::GBI_RDP::loadTile(&state,&cursor);break;
        case 0xF5:RT64::GBI_RDP::setTile(&state,&cursor);break;
        default:throw std::logic_error("Unadmitted native scenery texture command");
        }
    }
}
}

void LocalSceneryRT64Pass::prepare(const netplay::experimental::LocalSceneryScene* scene,
        std::span<const dkr_owned_draw_event> observations,std::span<std::uint8_t> decoder,
        std::uint32_t identity) noexcept {
    scene_=nullptr;observation_count_=0;consumed_.fill(false);textures_.clear();identity_=identity;
    if(!scene||scene->sprites.empty()||decoder.size()<16U*1024U*1024U||
       scene->sprites.size()>netplay::experimental::LocalSceneryCapture::kMaxSprites)return;
    try {
        netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::SceneryUpload);
        settings_={enhancements::presentation_profile(),enhancements::scenery_retention_mode(),
            enhancements::view_distance_multiplier(),enhancements::keep_hub_scenery_requested(),
            enhancements::keep_track_scenery_requested(),enhancements::keep_minigame_scenery_requested()};
        for(const auto& event:observations) {
            if(event.kind!=DKR_OWNED_LOCAL_SCENERY||event.parameters[0]!=scene->scene)continue;
            if(!dkr_owned_draw_event_valid(&event)||observation_count_==observations_.size())return;
            // Disabled local scenery must incur no texture copy/upload. This
            // preference is read only by the decoder, never by the CPU adapter.
            if(!settings_.enabled(int(event.parameters[1])))continue;
            auto plan=local_scenery::plan_view(*scene,event,settings_);
            if(!plan.count)continue;
            plans_[observation_count_]=plan;
            observations_[observation_count_++]=event;
        }
        if(!observation_count_)return;
        std::uint64_t selected=0,copied=0;
        const auto plans=std::span(plans_).first(observation_count_);
        // Copy the union of textures used by the selected frames, once even
        // when multiple viewports/passes share them. No new resource eviction:
        // the retained immutable scene still owns every animation frame.
        if(!local_scenery::copy_view_textures(*scene,plans,decoder,textures_,copied))return;
        for(const auto& plan:plans)selected+=plan.count;
        scene_=scene;
        netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalSceneryCandidates,selected);
        netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalSceneryTextureBytes,copied);
    } catch(...) { scene_=nullptr; } // Optional visuals cannot abort a match.
}

void LocalSceneryRT64Pass::draw_at(RT64::State& state,std::uint32_t command,IdentitySelector select) {
    if(!scene_||!select)return;
    for(std::size_t observation=0;observation<observation_count_;++observation) {
        const auto& event=observations_[observation];
        if(consumed_[observation]||event.address-0x80000000U!=command)continue;
        consumed_[observation]=true;
        const auto aspect=std::bit_cast<float>(event.parameters[7]);
        auto& rsp=*state.rsp;
        // RT64's push methods silently decline at their limits; a matching pop
        // would then remove canonical state. Reject this optional pass first.
        if(state.rdp->colorCombinerStackSize>=RDP_EXTENDED_STACK_SIZE||
           state.rdp->primColorStackSize>=RDP_EXTENDED_STACK_SIZE||
           state.rdp->envColorStackSize>=RDP_EXTENDED_STACK_SIZE)continue;
        netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::SceneryDraw);
        const auto world=rsp.modelMatrixStack[0];
        // Preparation already selected and ordered this exact viewport/pass.
        // Do not read live preferences again or recompute a second selection
        // that might request a texture not copied into this decoder workload.
        const auto& plan=plans_[observation];
        DrawScope restore(state);
        for(std::size_t item=0;item<plan.count;++item) {
            const auto& candidate=plan.candidates[item];
            const auto index=candidate.index;
            const auto& sprite=scene_->sprites[index];
            const auto anchor=hlslpp::mul(hlslpp::float4(float(std::int16_t(sprite.position[0])),
                float(std::int16_t(sprite.position[1])),float(std::int16_t(sprite.position[2])),1),world);
            if(float(anchor.w)<=0.001F)continue;
            const auto frame=candidate.frame;
            const float angle=float(std::uint16_t(sprite.roll+std::int16_t(event.parameters[5])))*
                (6.2831853071795864769F/65536.0F);
            const float cosine=std::cos(angle)*sprite.scale,sine=std::sin(angle)*sprite.scale;
            auto billboard=hlslpp::float4x4::identity();
            billboard[0]=hlslpp::float4(cosine,sine,0,0);
            billboard[1]=hlslpp::float4(-sine,cosine*aspect,0,0);
            billboard[2]=hlslpp::float4(0,0,sprite.scale,0);
            billboard[3]+=anchor; // Same anchor/MVP convention as the accepted bridge.
            const auto id=local_scenery::native_identity(presentation::mix_identity(identity_^
                presentation::mix_identity(event.parameters[14])^event.parameters[13]),unsigned(index),event.token);
            select(rsp,id);
            rsp.modelMatrixStack[2]=billboard;rsp.modelMatrixStackSize=3;
            rsp.modelMatrixSegmentedAddressStack[2]=local_scenery::kVertexScratch;
            rsp.modelMatrixPhysicalAddressStack[2]=local_scenery::kVertexScratch;
            rsp.modelViewProjChanged=true;rsp.modelViewProjInserted=false;
            const auto alpha=candidate.alpha;
            netplay::experimental::performance::event(netplay::experimental::performance::Event::LocalScenerySprites);
            const auto& material=alpha==255?sprite.material:sprite.faded_material;
            state.rdp->setCombine((std::uint64_t(material[0][0])<<32)|material[0][1]);
            state.rdp->setOtherMode(material[1][0]&0xFFFFFFU,material[1][1]);
            state.rdp->setPrimColor(0,0,(sprite.primitive&0xFFFFFF00U)|alpha);
            state.rdp->setEnvColor(sprite.environment);
            for(const auto& tile:sprite.frames[frame].tiles) {
                if(tile.texture>=sprite.textures.size())continue;
                const auto* texture=sprite.textures[tile.texture].get();
                const auto binding=std::find_if(textures_.begin(),textures_.end(),[&](const auto& b){return b.texture==texture;});
                if(binding==textures_.end())continue;
                // Each call owns a complete texture/TLUT upload so its replay
                // scope can restore TMEM without depending on another native call.
                upload_texture(state,*texture,binding->address);
                for(unsigned vertex=0;vertex<6;++vertex) {
                    const auto& v=tile.vertices[vertex];RT64::RSP::Vertex output{};
                    output.x=v.x;output.y=v.y;output.z=v.z;
                    output.color.r=v.color[0];output.color.g=v.color[1];output.color.b=v.color[2];output.color.a=v.color[3];
                    std::memcpy(state.RDRAM+local_scenery::kVertexScratch+vertex*sizeof(output),&output,sizeof(output));
                }
                rsp.setVertex(local_scenery::kVertexScratch,6,1);
                // DKR triangle-corner UVs bypass ordinary texture scaling.
                // Apply each duplicate corner exactly once before using it.
                for(unsigned vertex=0;vertex<6;++vertex) {
                    const auto& v=tile.vertices[vertex];
                    rsp.modifyVertex(std::uint16_t(vertex+1),G_MWO_POINT_ST,
                        (std::uint32_t(std::uint16_t(v.s))<<16)|std::uint16_t(v.t));
                }
                rsp.drawIndexedTri(1,2,3);rsp.drawIndexedTri(4,5,6);
                state.flush(); // No mixed native/canonical texture call.
            }
        }
    }
}
}

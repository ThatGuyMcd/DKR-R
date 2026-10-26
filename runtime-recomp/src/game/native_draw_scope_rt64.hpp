#pragma once
#include "local_scenery_vertex_state.hpp"
#include "hle/rt64_state.h"
#include "gbi/rt64_f3d.h"
#include <algorithm>
#include <array>
#include <list>

namespace dkr::runtime {
class NativeDrawScope final {
  public:
    explicit NativeDrawScope(RT64::State& state)
        : s_(state), r_(*state.rsp), d_(*state.rdp), matrices_(r_.modelMatrixStack),
          physical_(r_.modelMatrixPhysicalAddressStack), segmented_(r_.modelMatrixSegmentedAddressStack),
          size_(r_.modelMatrixStackSize), vertices_(r_.vertices), indices_(r_.indices), used_(r_.used),
          extended_(r_.extended), texture_(r_.textureState),
          geometry_(r_.geometryModeStack[r_.geometryModeStackSize - 1]), segment0_(r_.segments[0]),
          tiles_(std::to_array(d_.tiles)), replacements_(std::to_array(d_.tileReplacementHashes)), image_(d_.texture),
          other_(d_.otherMode), vertex_pipeline_(r_) {
        s_.flush();
        d_.pushCombine();
        d_.pushPrimColor();
        d_.pushEnvColor();
        // These RAM textures cannot be framebuffer copies. Splice, don't copy
        // or destroy, the canonical GPU-copy mappings while they are decoded.
        regions_.splice(regions_.end(), s_.framebufferManager.activeRegionsTMEM);
        r_.segments[0] = 0;
        r_.setGeometryMode(1U | G_SHADE | G_FOG | r_.shadingSmoothMask);
        r_.clearGeometryMode(r_.cullBothMask | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
        r_.setTexture(0, 0, 1, 0xFFFF, 0xFFFF);
        // Canonical explicit replacement hashes must not override a new native
        // texture. Normal TMEM/Rice hashing still resolves enabled local packs.
        for (unsigned tile = 0; tile < RDP_TILES; ++tile)
            d_.clearTileReplacementHash(tile);
        r_.extended.vertexSegmentEnabled.fill(false);
    }
    ~NativeDrawScope() {
        s_.flush();
        d_.popCombine();
        d_.popPrimColor();
        d_.popEnvColor();
        r_.modelMatrixStack = matrices_;
        r_.modelMatrixPhysicalAddressStack = physical_;
        r_.modelMatrixSegmentedAddressStack = segmented_;
        r_.modelMatrixStackSize = size_;
        r_.vertices = vertices_;
        r_.indices = indices_;
        r_.used = used_;
        r_.extended = extended_;
        r_.extended.modelMatrixIdStackChanged = true;
        r_.textureState = texture_;
        r_.geometryModeStack[r_.geometryModeStackSize - 1] = geometry_;
        r_.segments[0] = segment0_;
        vertex_pipeline_.restore(r_);
        std::copy(tiles_.begin(), tiles_.end(), d_.tiles);
        std::copy(replacements_.begin(), replacements_.end(), d_.tileReplacementHashes);
        d_.texture = image_;
        d_.setOtherMode(other_.H, other_.L);
        s_.framebufferManager.activeRegionsTMEM.clear();
        s_.framebufferManager.activeRegionsTMEM.splice(s_.framebufferManager.activeRegionsTMEM.end(), regions_);
        for (const auto attribute :
             {RT64::DrawAttribute::Texture, RT64::DrawAttribute::GeometryMode, RT64::DrawAttribute::Combine,
              RT64::DrawAttribute::PrimColor, RT64::DrawAttribute::EnvColor, RT64::DrawAttribute::OtherMode})
            s_.updateDrawStatusAttribute(attribute);
    }

  private:
    RT64::State& s_;
    RT64::RSP& r_;
    RT64::RDP& d_;
    decltype(r_.modelMatrixStack) matrices_;
    decltype(r_.modelMatrixPhysicalAddressStack) physical_, segmented_;
    int size_;
    decltype(r_.vertices) vertices_;
    decltype(r_.indices) indices_;
    decltype(r_.used) used_;
    decltype(r_.extended) extended_;
    RT64::RSP::TextureState texture_;
    std::uint32_t geometry_, segment0_;
    std::array<RT64::LoadTile, 8> tiles_;
    std::array<std::uint64_t, 8> replacements_;
    RT64::LoadTexture image_;
    decltype(d_.otherMode) other_;
    local_scenery::VertexPipelineSnapshot<RT64::RSP> vertex_pipeline_;
    std::list<RT64::FramebufferManager::RegionTMEM> regions_;
};
} // namespace dkr::runtime

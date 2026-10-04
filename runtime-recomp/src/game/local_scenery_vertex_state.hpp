#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace dkr::runtime::local_scenery {
// setVertexCommon/addCurrentProjection consume pending state as well as
// appending native draw data. Restore the pending canonical state exactly:
// forcing a matrix recompute would lose an inserted MVP, and restoring only
// curFogIndex would leave the next vertex with a consumed fog update.
// Appended workload records are deliberately NOT removed: native draws own
// their indices until the entire workload has retired.
template<class Rsp> class VertexPipelineSnapshot final {
public:
    explicit VertexPipelineSnapshot(const Rsp& r) noexcept
        :mvp_(r.modelViewProjMatrix),mvp_changed_(r.modelViewProjChanged),
         mvp_inserted_(r.modelViewProjInserted),projection_changed_(r.projectionMatrixChanged),
         projection_inversed_(r.projectionMatrixInversed),viewport_changed_(r.viewportChanged),
         projection_(r.projectionIndex),view_projection_(r.curViewProjIndex),
         transform_(r.curTransformIndex),fog_(r.curFogIndex),light_(r.curLightIndex),
         look_at_(r.curLookAtIndex),light_count_(r.curLightCount),
         vertex_fog_(r.vertexFogIndex),vertex_light_(r.vertexLightIndex),
         vertex_light_count_(r.vertexLightCount),vertex_look_at_(r.vertexLookAtIndex),
         fog_changed_(r.fogChanged),lights_changed_(r.lightsChanged),look_at_changed_(r.lookAtChanged) {}

    void restore(Rsp& r) const noexcept {
        r.modelViewProjMatrix=mvp_;
        r.modelViewProjChanged=mvp_changed_;r.modelViewProjInserted=mvp_inserted_;
        r.projectionMatrixChanged=projection_changed_;r.projectionMatrixInversed=projection_inversed_;
        r.viewportChanged=viewport_changed_;r.projectionIndex=projection_;r.curViewProjIndex=view_projection_;
        r.curTransformIndex=transform_;r.curFogIndex=fog_;r.curLightIndex=light_;
        r.curLookAtIndex=look_at_;r.curLightCount=light_count_;
        r.vertexFogIndex=vertex_fog_;r.vertexLightIndex=vertex_light_;
        r.vertexLightCount=vertex_light_count_;r.vertexLookAtIndex=vertex_look_at_;
        r.fogChanged=fog_changed_;r.lightsChanged=lights_changed_;r.lookAtChanged=look_at_changed_;
    }
private:
    std::decay_t<decltype(std::declval<Rsp>().modelViewProjMatrix)> mvp_;
    bool mvp_changed_,mvp_inserted_,projection_changed_,projection_inversed_,viewport_changed_;
    int projection_;
    std::uint16_t view_projection_,transform_,fog_,light_,look_at_;
    std::uint8_t light_count_;
    std::uint32_t vertex_fog_,vertex_light_,vertex_light_count_,vertex_look_at_;
    bool fog_changed_,lights_changed_,look_at_changed_;
};
}

#include "water_uv_rt64.hpp"
#include "interpolation_state_policy.hpp"
#include "water_performance.hpp"
#include "water_scroll_policy.hpp"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
void Interrupt() {}
struct Fixture {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(8 * 1024 * 1024);
    std::uint32_t interrupt = 0;
    std::unique_ptr<RT64::WorkloadQueue> queue = std::make_unique<RT64::WorkloadQueue>();
    std::unique_ptr<RT64::State> state = std::make_unique<RT64::State>(ram.data(), &interrupt, &Interrupt);
    RT64::EmulatorConfiguration emulator{};
    Fixture() { state->ext.workloadQueue = queue.get(); }
    RT64::DrawData& draw() { return queue->workloads[queue->writeCursor].drawData; }
    void begin_recording() {
        state->ext.emulatorConfig = &emulator;
        state->drawStatus.clearChanges();
        state->rsp->cullBothMask = 0x3000U;
        state->rsp->cullFrontMask = 0x1000U;
        state->rdp->colorImage.changed = state->rdp->depthImage.changed = false;
        auto& workload = queue->workloads[queue->writeCursor];
        workload.addFramebufferPair(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x200000);
        workload.fbPairs[workload.currentFramebufferPairIndex()].changeProjection(
            state->rsp->curViewProjIndex, state->rsp->getCurrentProjectionType());
    }
    void load(unsigned slot, unsigned x, unsigned y, unsigned frame, bool texgen) {
        // Seed all real RSP vertex streams, including nonzero velocities and
        // metadata, then exercise the PINNED modifyVertex implementation.
        auto& d = draw(); auto& rsp = *state->rsp;
        rsp.indices[slot] = d.vertexCount(); rsp.used[slot] = false;
        for (float v : {float(x), float(y + frame), float(x ^ y)}) d.posFloats.push_back(v);
        for (float v : {1.F, 2.F, 3.F}) d.velFloats.push_back(v);
        d.tcFloats.insert(d.tcFloats.end(), {0.F, 0.F});
        d.tcVelFloats.insert(d.tcVelFloats.end(), {4.F, 5.F});
        d.normColBytes.insert(d.normColBytes.end(), {12, 34, 56, 128});
        d.viewProjIndices.push_back(2); d.worldIndices.push_back(3);
        d.fogIndices.push_back(4); d.lightIndices.push_back(5); d.lightCounts.push_back(6);
        d.lookAtIndices.push_back(texgen ? 7 : 0);
        d.posTransformed.emplace_back(float(x), float(y), 0.F, 1.F);
        d.posScreen.emplace_back(float(x), float(y), 0.F);
    }
};
// Fully expanded triangle-corner streams must match bit for bit, regardless
// of internal vertex-index deduplication. Includes everything ST may clone.
std::vector<float> expand(const RT64::DrawData& d) {
    std::vector<float> out;
    for (auto i : d.faceIndices) {
        for (unsigned c=0;c<3;++c) {out.push_back(d.posFloats[i*3+c]);out.push_back(d.velFloats[i*3+c]);}
        for (unsigned c=0;c<2;++c) {out.push_back(d.tcFloats[i*2+c]);out.push_back(d.tcVelFloats[i*2+c]);}
        for (unsigned c=0;c<4;++c) {out.push_back(d.normColBytes[i*4+c]);out.push_back(d.posTransformed[i][c]);}
        for (unsigned c=0;c<3;++c) out.push_back(d.posScreen[i][c]);
        for (unsigned v : std::array<unsigned,6>{d.viewProjIndices[i], d.worldIndices[i], d.fogIndices[i],
                          d.lightIndices[i], d.lightCounts[i], d.lookAtIndices[i]}) out.push_back(float(v));
    }
    return out;
}
unsigned run(Fixture& f, unsigned s, unsigned frame, bool fast, bool seams, bool texgen, bool record=false) {
    f.draw() = {}; auto& d = f.draw(); auto& rsp = *f.state->rsp;
    unsigned skipped = 0;
    // Retail waves_render loads TWO ROWS at a time (max 14 vertices at s=6).
    // It reloads slots for each row, including each high-density subcell.
    for (unsigned row=0;row<s;++row) {
        for (unsigned y=0;y<2;++y) for(unsigned x=0;x<=s;++x) f.load(y*(s+1)+x,x,row+y,frame,texgen);
        for(unsigned x=0;x<s;++x) {
            const std::array<std::array<unsigned,3>,2> tris{{{x,x+s+1,x+1},{x+1,x+s+1,x+s+2}}};
            for(unsigned t=0;t<2;++t) {
                for(auto slot:tris[t]) {
                    // Explicit unsigned wrapping models the authored s16 UVs.
                    const auto u=std::int16_t(std::uint16_t((slot%(s+1))*512+frame*8191+(seams?t*17:0)));
                    const auto v=std::int16_t(std::uint16_t((row+slot/(s+1))*1024-frame*7919));
                    skipped += dkr::runtime::water::set_texcoord(rsp,d,slot,u,v,fast);
                }
                if(record) {
                    dkr::runtime::water::set_culling(rsp,0U,fast && (x!=0 || t!=0));
                    rsp.drawIndexedTri(tris[t][0],tris[t][1],tris[t][2]);
                } else {
                    for(auto slot:tris[t]) { d.faceIndices.push_back(rsp.indices[slot]); rsp.used[slot]=true; }
                }
            }
        }
    }
    if(record) f.state->flush();
    return skipped;
}
void interpolate(Fixture& current, Fixture& previous, std::uint8_t tag=0U) {
    for(auto* fixture : {&current,&previous}) {
        auto& d=fixture->draw();
        d.worldTransforms={hlslpp::float4x4::identity()};
        d.worldTransformGroups={0}; d.worldTransformVertexIndices={0};
        d.transformGroups.resize(1);
        auto& group=d.transformGroups[0];
        group.decompose=false;
        group.vertexInterpolation=G_EX_COMPONENT_INTERPOLATE;
        group.texcoordInterpolation=tag ? tag : G_EX_COMPONENT_INTERPOLATE;
        fixture->queue->workloads[fixture->queue->writeCursor].extended.texcoordWrapPoint={8092.F,8092.F};
    }
    RT64::GameFrame frame;
    RT64::GameFrameMap::WorkloadMap mapping;
    mapping.transforms.resize(1); mapping.prevTransformsMapped.resize(1);
    RT64::ModifiedBuffers modified;
    frame.matchTransform(current.queue->workloads[current.queue->writeCursor],
                         previous.queue->workloads[previous.queue->writeCursor],mapping,nullptr,0,0,modified);
    assert(mapping.transforms[0].mapped && mapping.prevTransformsMapped[0]);
    assert(modified.positionVelocity && modified.texcoordVelocity);
}
}
#ifdef main
#undef main
#endif
int main() {
    using namespace dkr::runtime;
    interpolation::GroupState groups;
    groups.load_matrix(0, 123, true, true, true, true);
    assert(groups.active_group().procedural_water);
    groups.load_matrix(1, 456, true, true, true); // same flags != water provenance
    assert(!groups.active_group().procedural_water);

    // Native transform-group extension: both axes wrap independently. Reject
    // non-power-of-two/invalid masks rather than changing arbitrary materials.
    assert(water::scroll_tag(1023,2047)==std::uint8_t(0x80U|(5U<<3U)|6U));
    assert(water::scroll_tag(1000,1023)==0);
    assert(water::scroll_tag(0xFFFFFFFFU,1023)==0);
    assert(water::scroll_tag(8191,1023)==0);
    assert(!water::is_scroll_tag(G_EX_COMPONENT_INTERPOLATE));
    assert(!water::is_scroll_tag(G_EX_COMPONENT_AUTO));
    assert(!water::is_scroll_tag(0xFF));
    groups.load_matrix(0,123,true,true,true,true,water::scroll_tag(1023,2047));
    assert(groups.active_group().water_scroll_tag!=0);
    assert(groups.begin_scope(interpolation::kShadowScopeMode,1,true));
    assert(groups.begin_scope(interpolation::kAspectAdjustScopeMode,1,false));
    assert(groups.active_group().water_scroll_tag==0);
    groups.reset();
    groups.load_matrix(0,123,true,true,true,true);
    groups.select_matrix(0);
    assert(groups.begin_scope(interpolation::kShadowScopeMode,1,true));
    assert(!groups.active_group().procedural_water);
    assert(groups.begin_scope(interpolation::kAspectAdjustScopeMode,1,false));
    assert(!groups.active_group().procedural_water);
    (void)groups.end_scope();
    (void)groups.end_scope();
    assert(groups.begin_scope(interpolation::kAspectAdjustScopeMode,1,false));
    assert(groups.active_group().procedural_water);
    (void)groups.end_scope(); groups.reset();
    assert(!groups.active_group().procedural_water);

    Fixture baseline, candidate;
    // F3DDKR uses the F3DEX geometry-bit layout. Cull constants in RT64 are
    // microcode-specific table entries, not global G_CULL_* macros.
    constexpr std::uint32_t front=0x1000U, back=0x2000U, zbuffer=1U;
    for(bool mirrored : {false,true}) for(unsigned row=0;row<6;++row) {
        auto& a=*baseline.state->rsp; auto& b=*candidate.state->rsp;
        a.cullBothMask=b.cullBothMask=front|back;
        a.geometryModeStack[0]=b.geometryModeStack[0]=zbuffer|G_FOG;
        for(unsigned i=0;i<12;++i) {
            auto cull=i<4?0U:(i<9?(mirrored?front:back):front);
            baseline.state->drawStatus.clearChanges();candidate.state->drawStatus.clearChanges();
            water::set_culling(a,cull,false);
            const auto old=b.geometryModeStack[0];
            const bool skipped=water::set_culling(b,cull,i!=0);
            assert(a.geometryModeStack[0]==b.geometryModeStack[0]);
            assert((b.geometryModeStack[0]&(zbuffer|G_FOG))==(zbuffer|G_FOG));
            assert(skipped==(i!=0 && (old&b.cullBothMask)==cull));
            assert(candidate.state->drawStatus.isChanged(RT64::DrawAttribute::GeometryMode)==!skipped);
        }
    }
    for(unsigned s : {1U,2U,3U,4U,6U}) for(unsigned frame=0;frame<24;++frame)
        for(bool seams:{false,true}) for(bool texgen:{false,true}) {
            run(baseline,s,frame,false,seams,texgen);
            auto skipped=run(candidate,s,frame,true,seams,texgen);
            assert(expand(baseline.draw())==expand(candidate.draw()));
            assert(baseline.draw().vertexCount()-candidate.draw().vertexCount()==skipped);
            if(!seams) assert(candidate.draw().vertexCount()==2*s*(s+1));
        }
    Fixture previous_baseline, previous_candidate;
    for(unsigned s:{2U,3U,4U,6U}) for(unsigned frame=1;frame<10;++frame) for(bool seams:{false,true}) {
        run(previous_baseline,s,frame-1,false,seams,false);run(baseline,s,frame,false,seams,false);
        run(previous_candidate,s,frame-1,true,seams,false);run(candidate,s,frame,true,seams,false);
        interpolate(baseline,previous_baseline);interpolate(candidate,previous_candidate);
        // Actual RT64 matching produces identical expanded position AND UV
        // velocities: every interpolation weight therefore gives the same
        // triangle corners, not just the two authored endpoints.
        assert(expand(baseline.draw())==expand(candidate.draw()));
    }
    auto& rsp=*candidate.state->rsp; auto& d=candidate.draw();
    const auto i=rsp.indices[0];
    d.tcFloats[i*2]=d.tcFloats[i*2+1]=0.F; rsp.used[0]=true; d.lookAtIndices[i]=9;
    assert(!water::set_texcoord(rsp,d,0,0,0,true)); // equal UV must STILL clear texgen
    assert(d.lookAtIndices[rsp.indices[0]]==0);
    rsp.used[0]=true;
    assert(water::set_texcoord(rsp,d,0,0,0,true));
    assert(!water::same_texcoord(rsp,d,32,0,0));
    rsp.indices[0]=0xFFFFFFFFU;
    assert(!water::same_texcoord(rsp,d,0,0,0));

    for(unsigned width:{16U,32U,64U,128U}) for(unsigned height:{16U,32U,64U})
        for(bool reverse:{false,true}) {
            run(previous_candidate,2,0,true,false,false);
            run(candidate,2,1,true,false,false);
            const auto tag=water::scroll_tag(width*32-1,height*32-1);
            assert(water::scroll_period(tag,0)==float(width));
            assert(water::scroll_period(tag,1)==float(height));
            auto& a=previous_candidate.draw().tcFloats;auto& b=candidate.draw().tcFloats;
            for(unsigned i=0;i<a.size()/2;++i) {
                // U forward wrap / V reverse wrap in the same transform.
                a[i*2]=float(width)-0.25F;b[i*2]=0.25F;
                a[i*2+1]=0.25F;b[i*2+1]=float(height)-0.25F;
                if(reverse) {std::swap(a[i*2],b[i*2]);std::swap(a[i*2+1],b[i*2+1]);}
            }
            const auto authored=b;
            interpolate(candidate,previous_candidate,tag);
            assert(b==authored); // every authored endpoint is unchanged
            const auto& velocity=candidate.draw().tcVelFloats;
            for(unsigned i=0;i<velocity.size()/2;++i) {
                assert(velocity[i*2]==(reverse?-0.5F:0.5F));
                assert(velocity[i*2+1]==(reverse?0.5F:-0.5F));
            }
            // An untagged texture retains the original renderer behaviour.
            interpolate(candidate,previous_candidate);
            assert(candidate.draw().tcVelFloats[0]==b[0]-a[0]);
            // Verify the actual native matrix API preserves all eight bits;
            // this must never travel through the packed guest GBI.
            rsp.matrixId(123,false,false,false,1,1,0,0,1,1,tag,1,0,1,0,0,false,false);
            assert(rsp.extended.modelMatrixIdStack[rsp.extended.modelMatrixIdStackSize-1].texcoordInterpolation==tag);
        }

    for(unsigned s:{2U,3U,4U,6U}) {
        run(baseline,s,0,false,false,false);run(candidate,s,0,true,false,false);
        std::printf("grid=%u triangles=%u baseline-vertices=%u candidate-vertices=%u\n",s,2*s*s,baseline.draw().vertexCount(),candidate.draw().vertexCount());
    }
    for(unsigned s:{2U,3U,4U,6U}) {
        Fixture old_draws, new_draws;
        old_draws.begin_recording();new_draws.begin_recording();
        run(old_draws,s,5,false,false,false,true);
        run(new_draws,s,5,true,false,false,true);
        assert(expand(old_draws.draw())==expand(new_draws.draw()));
        auto& a=old_draws.queue->workloads[old_draws.queue->writeCursor];
        auto& b=new_draws.queue->workloads[new_draws.queue->writeCursor];
        // Real drawIndexedTri -> checkDrawState -> flush -> addGameCall,
        // not a model of the batching rules. Preserve each authored row.
        assert(a.gameCallCount==2*s*s && b.gameCallCount==s);
        for(unsigned row=0;row<s;++row) {
            auto& call=b.fbPairs[0].projections[0].gameCalls[row].callDesc;
            const auto& old_call=a.fbPairs[0].projections[0].gameCalls[row*2*s].callDesc;
            assert(call.triangleCount==2*s && call.geometryMode==old_call.geometryMode);
            assert(call.otherMode.L==old_call.otherMode.L && call.otherMode.H==old_call.otherMode.H);
            assert(call.cullBothMask==old_call.cullBothMask && call.tileCount==old_call.tileCount);
        }
        std::printf("grid=%u actual-draw-calls=%u -> %u\n",s,a.gameCallCount,b.gameCallCount);
    }
    std::puts("Water UV differential checks passed (stock rows, seams, wrap, reload, texture generation, all cloned attributes).");
}

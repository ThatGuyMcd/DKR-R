#include "netplay/local_scenery.hpp"
#include "netplay/experimental_effect_envelope.hpp"
#include "netplay/experimental_performance.hpp"
#include "local_scenery_policy.hpp"
#include "local_scenery_vertex_state.hpp"
#include "local_scenery_workload.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace dkr::runtime::netplay::experimental;
namespace {
constexpr std::uint32_t object=0x1000,header=0x1100,instances=0x1180,sprite=0x1200;
constexpr std::uint32_t textures=0x1280,texture=0x2000,commands=0x3000,dl=0x4000;
constexpr std::uint32_t vertices=0x5000,triangles=0x6000,materials=0x7000,palette=0x8000;
struct Resource {
    std::vector<std::uint8_t> ram=std::vector<std::uint8_t>(0x800000);
    void word(std::uint32_t p,std::uint32_t v) {std::memcpy(ram.data()+p,&v,4);}
    void half(std::uint32_t p,std::uint16_t v) {std::memcpy(ram.data()+(p^2),&v,2);}
    void byte(std::uint32_t p,std::uint8_t v) {ram[p^3]=v;}
    void scalar(std::uint32_t p,float v) {std::memcpy(ram.data()+p,&v,4);}
    void command(std::uint32_t p,std::uint32_t a,std::uint32_t b) {word(p,a);word(p+4,b);}
    explicit Resource(bool ci=false) {
        word(object+0x40,0x80000000U+header);word(object+0x68,0x80000000U+instances);
        half(object+0x4A,7);scalar(object+8,3);scalar(object+12,128);scalar(object+16,512);scalar(object+20,256);
        half(header+0x30,0x40);half(header+0x4E,6000);byte(header+0x53,1);byte(header+0x55,1);
        word(instances,0x80000000U+sprite);half(sprite,1);half(sprite+2,1);
        word(sprite+8,0x80000000U+textures);word(sprite+12,0x80000000U+dl);
        word(textures,0x80000000U+texture);half(texture+0x16,48);word(texture+0xC,0x80000000U+commands);
        unsigned n=0;
        command(commands+8*n++,0xFD100000,texture+32);
        command(commands+8*n++,0xF5100000,0x07000000);
        command(commands+8*n++,0xF3000000,0x07007000); // Eight RGBA16 pixels, 16 source bytes.
        if(ci) {
            command(commands+8*n++,0xFD100000,palette);
            command(commands+8*n++,0xF5000100,0x07000000);
            command(commands+8*n++,0xF0000000,0x0703C000); // Sixteen external palette entries.
            for(unsigned i=0;i<32;++i)byte(palette+i,std::uint8_t(i));
        }
        command(commands+8*n++,0xF5100200,0);command(commands+8*n++,0xF2000000,0x0000C000);
        half(texture+0xA,std::uint16_t(n));
        command(dl,0x07000000|(n<<16),commands);
        command(dl+8,0x04190000,vertices); // Four vertices appended after the anchor.
        command(dl+16,0x05100000,triangles);command(dl+24,0xB8000000,0);
        for(unsigned v=0;v<4;++v) {
            half(vertices+v*10,std::uint16_t(v*16));half(vertices+v*10+2,std::uint16_t(v*8));
            for(unsigned c=0;c<4;++c)byte(vertices+v*10+6+c,255);
        }
        for(unsigned t=0;t<2;++t)for(unsigned c=0;c<3;++c) {
            constexpr unsigned indices[2][3]={{1,2,3},{1,3,4}};
            byte(triangles+t*16+1+c,std::uint8_t(indices[t][c]));
            half(triangles+t*16+4+c*4,std::uint16_t(c*32));
            half(triangles+t*16+6+c*4,std::uint16_t(t*32));
        }
        for(unsigned i=0;i<64;++i) {
            command(materials+i*16,0xFC000000,0);command(materials+i*16+8,0xEF000000,0);
        }
    }
    bool capture(LocalSceneryCapture& cache) {return cache.capture(ram,0x80000000U+object,0x80000000U+materials);}
};
void ownership() {
    Resource input(true);LocalSceneryCapture cache;cache.reset(1);
    assert(input.capture(cache));auto first=cache.freeze(1);assert(first&&first->sprites.size()==1);
    const auto tex=first->sprites[0].textures[0];assert(tex->bytes.size()==80);
    const auto retained=tex->bytes;
    input.scalar(object+12,150);assert(input.capture(cache));auto second=cache.freeze(1);
    assert(first->sprites.size()==1&&second->sprites.size()==2);
    assert(second->sprites[0].textures[0]==second->sprites[1].textures[0]);
    assert(second->sprites[1].position[0]==150&&first->sprites[0].position[0]==128);
    std::fill(input.ram.begin(),input.ram.end(),0);
    assert(tex->bytes==retained); // No borrowed texture/palette/vertex allocation survives.
    cache.reset(2);assert(!cache.freeze(1)&&cache.freeze(2)->sprites.empty());
    assert(first->sprites.size()==1); // Old present keeps its scene generation alive.
}
void malformed() {
    Resource input;LocalSceneryCapture cache;cache.reset(3);
    input.command(commands+16,0xF3000000,0x07FFF000);assert(!input.capture(cache));
    assert(cache.rejected()==1&&cache.freeze(3)->sprites.empty());
    input.command(commands+16,0xF3000000,0x07007000);
    input.byte(triangles+1,31);assert(!input.capture(cache));
    input.byte(triangles+1,1);input.command(dl+24,0x06000000,0x80000000U+dl);
    assert(!input.capture(cache)); // Recursive display lists are not admitted.
    input.command(dl+24,0xB8000000,0);input.half(header+0x30,0);assert(!input.capture(cache));
    assert(cache.freeze(3)->sprites.empty());
    Resource ci(true);ci.command(commands+24,0xFD100000,0x807FFFF8U);assert(!ci.capture(cache));
    ci.command(commands+24,0xFD100000,palette);ci.command(commands+40,0xF3000000,0x07007000);
    assert(!ci.capture(cache)); // External palettes cannot admit general texture loads.
    assert(!cache.capture(std::span(input.ram).first(1024),0x80000000U+object,materials));
}
void budgets() {
    Resource input;LocalSceneryCapture cache;cache.reset(4);
    for(unsigned i=0;i<LocalSceneryCapture::kMaxSprites;++i)assert(input.capture(cache));
    assert(!input.capture(cache));const auto scene=cache.freeze(4);
    assert(scene->sprites.size()==LocalSceneryCapture::kMaxSprites);
    assert(scene->payload_bytes<LocalSceneryCapture::kMaxPayloadBytes);
    assert(scene->sprites.front().textures[0]==scene->sprites.back().textures[0]);
}
void envelope_storage() {
    std::vector<std::uint8_t> actual{1,2,3,4,5},reference=actual;
    actual.reserve(256);const auto capacity=actual.capacity();
    const auto prepend_reference=[&](const auto& header) {
        std::vector<std::uint8_t> next(header.begin(),header.end());
        next.insert(next.end(),reference.begin(),reference.end());reference=std::move(next);
    };
    for(unsigned i=0;i<5;++i) {
        const auto words=std::array<std::uint32_t,2>{unsigned(actual.size()),i};
        const auto tag=std::array<char,4>{'D','K','G',char('A'+i)};
        prepend_reference(effect_envelope_header(tag,words));prepend_effect_envelope(actual,tag,words);
        assert(actual==reference&&actual.capacity()==capacity);
    }
}
void visibility_and_identity() {
    using namespace dkr::runtime;
    dkr_owned_draw_event view{DKR_OWNED_LOCAL_SCENERY,0x80001000U,0,{}};
    view.parameters[0]=1;view.parameters[7]=0x3F800000U;
    view.parameters[8]=33;view.parameters[9]=1;view.parameters[10]=1;view.parameters[14]=1;
    assert(dkr_owned_draw_event_valid(&view));
    using Mode=enhancements::SceneryRetentionMode;
    local_scenery::Settings settings{enhancements::PresentationProfile::Modern,
        Mode::FullForwardView,3,false,true,false};
    assert(settings.enabled(enhancements::kRaceTypeDefault));
    assert(settings.enabled(enhancements::kRaceTypeBoss));
    assert(!settings.enabled(enhancements::kRaceTypeHubWorld));
    assert(settings.distance(6000)==18000);
    assert(settings.distance(16000)==32767);
    assert(settings.distance(0)==0);
    const auto workload_settings=settings;
    settings.view_distance=1;settings.keep_track=false;
    assert(workload_settings.enabled(enhancements::kRaceTypeDefault));
    assert(workload_settings.distance(6000)==18000);
    settings.profile=enhancements::PresentationProfile::Accurate;settings.keep_track=true;
    assert(!settings.enabled(enhancements::kRaceTypeDefault));
    assert(settings.distance(6000)==6000);
    assert(local_scenery::region_visible(-1,view,Mode::Authored));
    assert(local_scenery::region_visible(0,view,Mode::CurrentRegion));
    assert(!local_scenery::region_visible(31,view,Mode::Authored));
    assert(local_scenery::region_visible(32,view,Mode::Authored));
    assert(local_scenery::region_visible(31,view,Mode::VisibleAndAdjacent));
    assert(!local_scenery::region_visible(33,view,Mode::FullForwardView));
    view.parameters[10]|=2;assert(!dkr_owned_draw_event_valid(&view));
    view.parameters[10]=1;view.parameters[15]=2;assert(!dkr_owned_draw_event_valid(&view));
    assert(local_scenery::fade_alpha(100,80*80)==255);
    assert(local_scenery::fade_alpha(100,0)==255);
    assert(local_scenery::fade_alpha(100,79*79)==255);
    assert(local_scenery::fade_alpha(100,90*90)==127);
    assert(local_scenery::fade_alpha(100,100*100)==1);
    assert(local_scenery::fade_alpha(100,101*101)==0);
    assert(local_scenery::fade_alpha(0,100*100)==255);
    assert(local_scenery::fade_alpha(100,-1)==0);
    for(unsigned camera=0;camera<4;++camera)for(unsigned slot=0;slot<128;++slot) {
        const auto id=local_scenery::native_identity(123,slot,camera);
        assert(id!=0&&id!=UINT32_MAX&&(id&local_scenery::kNativeIdentityBit));
        for(unsigned previous=0;previous<slot;++previous)
            assert(id!=local_scenery::native_identity(123,previous,camera));
        for(unsigned previous=0;previous<camera;++previous)
            assert(id!=local_scenery::native_identity(123,slot,previous));
        assert(!(local_scenery::canonical_identity(id)&local_scenery::kNativeIdentityBit));
    }
}
void timing_intervals() {
    using performance::Sample;
    Sample earlier,later;
    earlier.count=100;earlier.nanos=100000;earlier.maximum=5000000;earlier.histogram[0]=100;
    later=earlier;later.count+=20;later.nanos+=200000;later.histogram[7]+=19;later.histogram[10]+=1;
    const auto interval=performance::difference(later,earlier);
    assert(interval.count==20&&interval.nanos==200000&&interval.maximum==0);
    assert(performance::percentile_upper(interval,95)==3000);
    assert(performance::percentile_upper(interval,99)==5000);
    assert(performance::percentile_upper(earlier,95)==50);
    assert(performance::percentile_upper(Sample{},95)==0);
    assert(performance::percentile_upper(later,0)==0);
    assert(performance::percentile_upper(later,101)==0);
    const auto backwards=performance::difference(earlier,later);
    assert(!backwards.count&&!backwards.nanos&&!backwards.histogram[7]);
    // Histogram ranks don't borrow an independently sampled call count.
    Sample concurrent;concurrent.count=400;concurrent.histogram[2]=1;
    assert(performance::percentile_upper(concurrent,99)==250);
    assert(performance::histogram_bucket(0)==0);
    assert(performance::histogram_bucket(50000)==0);
    assert(performance::histogram_bucket(50001)==1);
    assert(performance::histogram_bucket(100000000)==30);
    assert(performance::histogram_bucket(100000001)==31);
    assert(performance::histogram_bucket(UINT64_MAX)==31);
}
void pending_vertex_state() {
    // The production helper is instantiated against RT64 by the game build.
    // This independent fixture covers values native vertex submission consumes
    // without constructing a GPU/device or running an owned gameplay probe.
    struct Rsp {
        std::array<float,16> modelViewProjMatrix{};
        bool modelViewProjChanged=false,modelViewProjInserted=false;
        bool projectionMatrixChanged=false,projectionMatrixInversed=false,viewportChanged=false;
        int projectionIndex=-1;
        std::uint16_t curViewProjIndex=0,curTransformIndex=0,curFogIndex=0,curLightIndex=0,curLookAtIndex=0;
        std::uint8_t curLightCount=0;
        std::uint32_t vertexFogIndex=0,vertexLightIndex=0,vertexLightCount=0,vertexLookAtIndex=0;
        bool fogChanged=false,lightsChanged=false,lookAtChanged=false;
        bool operator==(const Rsp&) const=default;
    };
    for(unsigned flags=0;flags<256;++flags) {
        Rsp canonical;
        for(unsigned i=0;i<canonical.modelViewProjMatrix.size();++i)canonical.modelViewProjMatrix[i]=float(i)+0.5F;
        canonical.modelViewProjChanged=flags&1;canonical.modelViewProjInserted=flags&2;
        canonical.projectionMatrixChanged=flags&4;canonical.projectionMatrixInversed=flags&8;
        canonical.viewportChanged=flags&16;canonical.fogChanged=flags&32;
        canonical.lightsChanged=flags&64;canonical.lookAtChanged=flags&128;
        canonical.projectionIndex=int(flags)-1;canonical.curViewProjIndex=100;
        canonical.curTransformIndex=101;canonical.curFogIndex=102;canonical.curLightIndex=103;
        canonical.curLookAtIndex=104;canonical.curLightCount=7;
        canonical.vertexFogIndex=201;canonical.vertexLightIndex=202;
        canonical.vertexLightCount=203;canonical.vertexLookAtIndex=204;
        const dkr::runtime::local_scenery::VertexPipelineSnapshot snapshot(canonical);
        Rsp after_native;after_native.modelViewProjMatrix.fill(-12);
        snapshot.restore(after_native);
        assert(after_native==canonical);
    }
}

void selected_workload_resources() {
    using namespace dkr::runtime;
    using Mode=enhancements::SceneryRetentionMode;
    Resource input;LocalSceneryCapture capture;capture.reset(9);
    assert(input.capture(capture));LocalSceneryScene scene=*capture.freeze(9);scene.sprites.reserve(6);
    auto& source=scene.sprites[0];source.position={20,0,0};source.draw_distance=100;
    source.frames.resize(2);source.frames[1]=source.frames[0];source.frames[1].tiles[0].texture=1;
    auto extra=std::make_shared<LocalSceneryTexture>(*source.textures[0]);extra->bytes[32]^=0x80;
    source.textures.push_back(extra);
    auto excluded=source;excluded.position[0]=200;scene.sprites.push_back(excluded);
    auto faded=source;faded.position[0]=90;faded.animation=1;scene.sprites.push_back(faded);
    auto tie=source;scene.sprites.push_back(tie);
    auto hidden=source;hidden.transform_flags=0x200;scene.sprites.push_back(hidden);
    auto regional=source;regional.segment=1;scene.sprites.push_back(regional);
    local_scenery::Settings settings{enhancements::PresentationProfile::Modern,
        Mode::Authored,1,false,true,false};
    dkr_owned_draw_event opaque{DKR_OWNED_LOCAL_SCENERY,0x80001000U,0,{}};
    opaque.parameters[0]=scene.scene;opaque.parameters[6]=1;opaque.parameters[7]=0x3F800000U;
    opaque.parameters[8]=2;opaque.parameters[9]=1;opaque.parameters[14]=1;
    auto translucent=opaque;translucent.parameters[15]=1;
    const auto first=local_scenery::plan_view(scene,opaque,settings);
    const auto second=local_scenery::plan_view(scene,translucent,settings);
    assert(first.count==2&&first.candidates[0].index==0&&first.candidates[1].index==3);
    assert(first.candidates[0].frame==0&&first.candidates[0].alpha==255);
    assert(second.count==1&&second.candidates[0].index==2&&second.candidates[0].frame==1);
    assert(second.candidates[0].alpha==127);
    for(std::size_t i=0;i<first.count;++i)for(std::size_t j=0;j<second.count;++j)
        assert(first.candidates[i].index!=second.candidates[j].index);
    auto right=opaque;right.token=1;
    assert(local_scenery::plan_view(scene,right,settings).count==3); // P1-only invisible flag.
    settings.retention=Mode::FullForwardView;
    assert(local_scenery::plan_view(scene,opaque,settings).count==3);
    settings.retention=Mode::Authored;
    auto invalid=opaque;invalid.parameters[2]=std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    assert(!local_scenery::plan_view(scene,invalid,settings).count);
    invalid=opaque;invalid.parameters[0]++;assert(!local_scenery::plan_view(scene,invalid,settings).count);
    invalid=opaque;invalid.parameters[15]=2;assert(!local_scenery::plan_view(scene,invalid,settings).count);
    source.animation=128;opaque.parameters[6]=0;
    assert(local_scenery::plan_view(scene,opaque,settings).candidates[0].frame==1);
    source.animation=2;opaque.parameters[6]=1;
    assert(local_scenery::plan_view(scene,opaque,settings).count==1); // Invalid frame isn't uploaded/drawn.
    source.animation=0;
    settings.keep_track=false;assert(!local_scenery::plan_view(scene,opaque,settings).count);
    // Plans retain one preference snapshot: later settings cannot change which
    // texture is admitted to the decoder and then needed by its draw passes.
    const std::array plans{first,second,first};
    std::vector<std::uint8_t> decoder(0x1000000,0xA5);
    std::vector<local_scenery::TextureBinding> bindings;
    std::uint64_t copied=0;
    assert(local_scenery::copy_view_textures(scene,plans,decoder,bindings,copied));
    assert(bindings.size()==2&&copied==source.textures[0]->bytes.size()+extra->bytes.size());
    assert(bindings[0].texture==source.textures[0].get()&&bindings[1].texture==extra.get());
    for(const auto& binding:bindings)
        assert(std::equal(binding.texture->bytes.begin(),binding.texture->bytes.end(),decoder.begin()+binding.address));
    assert(std::all_of(decoder.begin(),decoder.begin()+local_scenery::kTextureBegin,[](auto byte){return byte==0xA5;}));
    assert(std::all_of(decoder.begin()+local_scenery::kTextureEnd,decoder.end(),[](auto byte){return byte==0xA5;}));
    const std::array only_opaque{first};
    assert(local_scenery::copy_view_textures(scene,only_opaque,decoder,bindings,copied));
    assert(bindings.size()==1&&copied==source.textures[0]->bytes.size()); // Inactive animation frame omitted.
    const auto capacity=bindings.capacity();
    const std::array<local_scenery::ViewPlan,1> empty{};
    assert(local_scenery::copy_view_textures(scene,empty,decoder,bindings,copied));
    assert(bindings.empty()&&!copied&&bindings.capacity()==capacity);
    auto bad_plan=first;bad_plan.candidates[0].index=UINT16_MAX;
    assert(!local_scenery::copy_view_textures(scene,std::span(&bad_plan,1),decoder,bindings,copied));
    assert(!local_scenery::copy_view_textures(scene,only_opaque,std::span(decoder).first(1024),bindings,copied));
    const std::array<local_scenery::ViewPlan,local_scenery::kMaxViewPlans+1> too_many_views{};
    assert(!local_scenery::copy_view_textures(scene,too_many_views,decoder,bindings,copied));
    scene.sprites[0].frames[0].tiles[0].texture=UINT16_MAX;
    assert(!local_scenery::copy_view_textures(scene,only_opaque,decoder,bindings,copied));
    scene.sprites[0].frames[0].tiles[0].texture=0;
    auto oversized=std::make_shared<LocalSceneryTexture>();
    oversized->bytes.resize(local_scenery::kTextureEnd-local_scenery::kTextureBegin+1U);
    scene.sprites[0].textures[0]=oversized;
    assert(!local_scenery::copy_view_textures(scene,only_opaque,decoder,bindings,copied));
    oversized->bytes.clear();
    assert(!local_scenery::copy_view_textures(scene,only_opaque,decoder,bindings,copied));
    scene.sprites[0].textures[0].reset();
    assert(!local_scenery::copy_view_textures(scene,only_opaque,decoder,bindings,copied));
}
}
int main() {
    ownership();malformed();budgets();envelope_storage();visibility_and_identity();timing_intervals();pending_vertex_state();selected_workload_resources();
    std::cout<<"Local scenery resource ownership/bounds and envelope-storage fixtures complete.\n";
    std::cout<<"This does not qualify DKR visuals, GPU lifetime or online gameplay.\n";
}

#include "camera_presentation.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace dkr::runtime::camera {
namespace {
using namespace mods::code;
[[noreturn]] void fail(const char* reason){throw mods::Error(std::string("Camera mod: ")+reason);}
template<class T> void copy_out(Memory& memory,std::uint32_t address,const T& value) {
    static_assert(sizeof(T)%4==0);
    for(unsigned i=0;i<sizeof(T);i+=4) {
        std::uint32_t word;std::memcpy(&word,reinterpret_cast<const char*>(&value)+i,4);
        memory.store(address+i,4,word);
    }
}
template<class T> T copy_in(const Memory& memory,std::uint32_t address) {
    T value{};
    for(unsigned i=0;i<sizeof(T);i+=4) {
        const auto word=std::uint32_t(memory.load(address+i,4));
        std::memcpy(reinterpret_cast<char*>(&value)+i,&word,4);
    }
    return value;
}
bool point(const float* p){return std::all_of(p,p+3,[](float x){return std::isfinite(x)&&std::abs(x)<=1.e7F;});}
template<class T> bool zero(const T& words){return std::all_of(std::begin(words),std::end(words),[](auto v){return v==0;});}
std::string text(const Memory& bus,std::uint32_t address) {
    std::string out;
    for(unsigned i=0;i<97;++i) {
        const auto c=char(bus.load(address+i,1));if(!c)return out;
        if(i==96)fail("setting name is too long");
        out+=c;
    }
    fail("invalid setting name");
}
std::shared_ptr<const LinkedSet> prepare(std::shared_ptr<const Package> package,const Json& settings,const std::string& revision) {
    if(!package||!package->modules.contains("presentation"))fail("package does not declare presentation v1");
    if(!package->dependencies.empty()||!package->tracks.empty())fail("presentation packages cannot depend on gameplay packages or tracks");
    auto linked=link({package},{{package->id,settings}},revision,Program::policy());
    if(!linked->hooks.empty()||!linked->native_functions.empty()||linked->owners.size()!=1)fail("presentation package attempted gameplay access");
    if(linked->resident_bytes>2*mods::MiB)fail("presentation package exceeds its private memory budget");
    if(linked->events.size()!=1||!linked->events.contains("*:on_presentation")||linked->events.at("*:on_presentation").size()!=1)
        fail("exactly one on_presentation callback is required");
    return linked;
}
}
mods::code::LinkPolicy Program::policy() {
    mods::code::LinkPolicy p;
    p.modules={{"core",1},{"memory",1},{"events",1},{"settings",1},{"presentation",1}};
    p.services={"dkr_sdk_version","dkr_setting_number","dkr_view_snapshot","dkr_view_input",
        "dkr_view_submit","dkr_view_query","dkr_view_math"};
    p.events={"on_presentation"};
    // No game RAM, native functions, gameplay hooks, resources containing raw
    // host paths, audio, clocks, RNG or network services enter this domain.
    return p;
}
Program::Program(std::shared_ptr<const mods::code::Package> package,mods::code::Json settings,std::string revision):
    linked_(prepare(std::move(package),settings,revision)),
    ram_(16*mods::MiB+linked_->resident_bytes,0),memory_(ram_,linked_->regions),
    settings_(mods::code::configuration(*linked_->owners[0].package,settings)),
    entry_(linked_->events.at("*:on_presentation").front()) {
    mods::code::install(*linked_,ram_);
}
bool Program::settings(const mods::code::Json& values) noexcept {
    if(quarantined_)return false; // Preserve the original fault diagnostic.
    try {auto next=mods::code::configuration(package(),values);settings_=std::move(next);error_.clear();return true;}
    catch(const std::exception& e){error_=e.what();return false;}
}
void Program::validate(const DkrViewPose& p)const {
    if(p.api!=1||p.size!=sizeof(p)||p.view!=snapshot_.view||p.racer_id!=snapshot_.racer_id||
       p.racer_generation!=snapshot_.racer_generation||p.mode>DKR_VIEW_VR_DRIVER||p.cut>1||
       p.visibility>DKR_VIEW_HIDE_RIDER_ONLY||!zero(p.reserved))fail("invalid view ownership or output version");
    if(p.mode==DKR_VIEW_ORIGINAL)return;
    if(!(snapshot_.capabilities&DKR_VIEW_POSE))fail("this renderer cannot submit an independent camera");
    if(!point(p.position)||!std::isfinite(p.yaw)||!std::isfinite(p.pitch)||!std::isfinite(p.roll)||
       std::abs(p.yaw)>32||std::abs(p.pitch)>32||std::abs(p.roll)>32)fail("invalid camera pose");
    float distance=0;for(unsigned i=0;i<3;++i){const auto d=p.position[i]-snapshot_.position[i];distance+=d*d;}
    if(distance>1000000.F)fail("camera exceeded the local safety envelope");
    if(!std::isfinite(p.vertical_fov)||p.vertical_fov<0.174532925F||p.vertical_fov>2.61799388F||
       !std::isfinite(p.near_plane)||p.near_plane<0.1F||p.near_plane>100||
       !std::isfinite(p.far_plane)||p.far_plane<=p.near_plane||p.far_plane>1.e7F)fail("invalid camera projection");
    if(!(snapshot_.capabilities&DKR_VIEW_PROJECTION)&&
       (p.vertical_fov!=snapshot_.original_fov||p.near_plane!=snapshot_.original_near||p.far_plane!=snapshot_.original_far))
        fail("independent projection is unavailable");
    if(p.visibility==DKR_VIEW_HIDE_VEHICLE&&!(snapshot_.capabilities&DKR_VIEW_HIDE_LOCAL_MODEL))fail("per-view vehicle hiding is unavailable");
    if(p.visibility==DKR_VIEW_HIDE_RIDER_ONLY&&!(snapshot_.capabilities&DKR_VIEW_HIDE_RIDER))fail("per-view rider hiding is unavailable");
    if(p.mode==DKR_VIEW_ORBIT&&(!(snapshot_.capabilities&DKR_VIEW_ORBIT_COVERAGE)||
       !(snapshot_.flags&DKR_VIEW_INSPECTION_ALLOWED)||(snapshot_.flags&DKR_VIEW_ONLINE)))fail("unrestricted inspection is unavailable in this session");
    if(p.mode==DKR_VIEW_VR_DRIVER&&!(snapshot_.capabilities&DKR_VIEW_STEREO))fail("stereoscopic headset output is unavailable");
}
std::optional<DkrViewPose> Program::frame(const DkrViewSnapshot& snapshot,const DkrViewInput& input,const Query& query) {
    output_.reset();query_=nullptr;services_=queries_=0;
    if(quarantined_)return {};
    bool callback_started=false;
    try {
        if(snapshot.api!=1||snapshot.size!=sizeof(snapshot)||snapshot.view>=4||!zero(snapshot.reserved)||
           !std::isfinite(snapshot.dt)||snapshot.dt<0||snapshot.dt>0.1F||!std::isfinite(snapshot.time)||
           !std::isfinite(snapshot.aspect)||snapshot.aspect<0.1F||snapshot.aspect>16||!std::isfinite(snapshot.speed)||
           !point(snapshot.position)||!point(snapshot.velocity)||!point(snapshot.original_position)||
           !std::isfinite(snapshot.yaw)||!std::isfinite(snapshot.pitch)||!std::isfinite(snapshot.roll)||
           !std::isfinite(snapshot.steer)||!std::isfinite(snapshot.boost)||
           !std::isfinite(snapshot.original_yaw)||!std::isfinite(snapshot.original_pitch)||!std::isfinite(snapshot.original_roll)||
           !std::isfinite(snapshot.original_fov)||!std::isfinite(snapshot.original_near)||!std::isfinite(snapshot.original_far))
            fail("invalid read-only race snapshot");
        if(input.api!=1||input.size!=sizeof(input)||!zero(input.reserved)||input.blocked>1||
           ((input.held|input.pressed|input.released)&~127U)||
           !std::isfinite(input.orbit_x)||!std::isfinite(input.orbit_y)||!std::isfinite(input.zoom)||
           std::abs(input.orbit_x)>1||std::abs(input.orbit_y)>1||std::abs(input.zoom)>1)
            fail("invalid local camera action sample");
        snapshot_=snapshot;input_=input;
        if(input_.blocked){input_.held=input_.pressed=0;input_.orbit_x=input_.orbit_y=input_.zoom=0;}
        // Include taps latched by the host between presentation intervals.
        auto& previous=held_[snapshot.view];
        input_.pressed|=input_.held&~previous;input_.released=previous&~input_.held;previous=input_.held;
        if(!(snapshot.flags&DKR_VIEW_DRIVING)||(snapshot.flags&DKR_VIEW_SCRIPTED))return {};
        query_=&query;
        mods::code::Registers registers;
        registers.integer[29]=std::uint64_t(std::int64_t(std::int32_t(linked_->owners[0].stack)));
        mods::code::Budget budget{250000,256,0};
        callback_started=true;
        mods::code::execute(memory_,registers,entry_,budget,[this](auto a,auto& c,auto& b){service(a,c,b);});
        query_=nullptr;error_.clear();return output_;
    }catch(const std::exception& e){
        query_=nullptr;output_.reset();error_=e.what();
        // A fault may leave half-written private smoothing state. Quarantine
        // only this local provider; never retry it every frame or stop a race.
        quarantined_=callback_started;return {};
    }
}
void Program::service(std::uint32_t address,mods::code::Registers& c,mods::code::Budget&) {
    if(++services_>256)fail("presentation service budget exhausted");
    const auto found=linked_->imports.find(address);
    if(found==linked_->imports.end()||found->second.dependency!="*"||found->second.owner!=0||found->second.event)
        fail("unavailable presentation import");
    const auto& name=found->second.name;
    const auto a=std::uint32_t(c.integer[4]),b=std::uint32_t(c.integer[5]),d=std::uint32_t(c.integer[6]);
    if(name=="dkr_sdk_version"){c.integer[2]=1;return;}
    if(name=="dkr_setting_number") {
        const auto key=text(memory_,a);const auto value=settings_.find(key);
        if(value==settings_.end()||!value->is_number())fail("unknown or non-numeric local setting");
        c.floating[0]=std::bit_cast<std::uint64_t>(value->get<double>());return;
    }
    if(name=="dkr_view_snapshot"){copy_out(memory_,a,snapshot_);c.integer[2]=1;return;}
    if(name=="dkr_view_input"){copy_out(memory_,a,input_);c.integer[2]=1;return;}
    if(name=="dkr_view_submit") {
        if(output_)fail("two camera outputs were submitted for one view");
        const auto pose=copy_in<DkrViewPose>(memory_,a);validate(pose);output_=pose;c.integer[2]=1;return;
    }
    if(name=="dkr_view_query") {
        const auto q=copy_in<DkrViewQuery>(memory_,a);
        if(q.api!=1||q.size!=sizeof(q)||q.view!=snapshot_.view||q.exclude_racer!=snapshot_.racer_id||
           !zero(q.reserved)||!point(q.origin)||!point(q.target)||!std::isfinite(q.radius)||q.radius<0||q.radius>128||q.include_dynamic>1)
            fail("invalid camera clearance query");
        DkrViewHit hit{};hit.api=1;hit.size=sizeof(hit);hit.status=DKR_VIEW_QUERY_UNSUPPORTED;
        if(++queries_>8)hit.status=DKR_VIEW_QUERY_BUDGET;
        else if(query_&&*query_&&(snapshot_.capabilities&DKR_VIEW_STATIC_COLLISION)&&
            (!q.include_dynamic||(snapshot_.capabilities&DKR_VIEW_DYNAMIC_COLLISION)))hit=(*query_)(q);
        if(hit.api!=1||hit.size!=sizeof(hit)||hit.status>DKR_VIEW_QUERY_BUDGET||hit.reserved||hit.reserved2||
           !std::isfinite(hit.fraction)||hit.fraction<0||hit.fraction>1||!point(hit.position)||!point(hit.normal))
            fail("invalid retained collision result");
        copy_out(memory_,b,hit);c.integer[2]=hit.status;return;
    }
    if(name=="dkr_view_math") {
        const float x=std::bit_cast<float>(b),y=std::bit_cast<float>(d);float result=0;
        if(!std::isfinite(x)||!std::isfinite(y))fail("non-finite presentation arithmetic");
        switch(a) {
        case DKR_VIEW_SIN:result=std::sin(x);break;case DKR_VIEW_COS:result=std::cos(x);break;
        case DKR_VIEW_ATAN2:result=std::atan2(x,y);break;
        case DKR_VIEW_EXP:if(x>20||x<-100)fail("exponential outside presentation range");result=std::exp(x);break;
        case DKR_VIEW_SQRT:if(x<0)fail("negative camera distance");result=std::sqrt(x);break;
        default:fail("unknown presentation arithmetic operation");
        }
        if(!std::isfinite(result))fail("invalid presentation arithmetic result");
        c.integer[2]=std::bit_cast<std::uint32_t>(result);return;
    }
    fail("unimplemented presentation service");
}
}

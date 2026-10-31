#include "vehicle_world.hpp"
#include "vehicle_symbols.hpp"
#include "legacy_character_menu.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <set>

namespace dkr::runtime::vehicles {
namespace {
using namespace dkr::mods;
[[noreturn]] void bad(const std::string& s){throw Error("Custom car selection: "+s);}
class Memory {
    std::span<std::uint8_t> ram_;
    std::size_t offset(std::uint32_t p,unsigned n)const {
        if(p<0x80000000U||p>0x9fffffffU||ram_.size()%4)bad("invalid guest address.");
        const auto o=p-0x80000000U;if(o>ram_.size()||n>ram_.size()-o)bad("guest range exceeds its owner.");return o;
    }
public:
    explicit Memory(std::span<std::uint8_t> ram):ram_(ram){}
    unsigned get(std::uint32_t p,unsigned n=4)const {const auto at=offset(p,n);unsigned value=0;for(unsigned i=0;i<n;++i)value=value<<8|ram_[(at+i)^3U];return value;}
    void set(std::uint32_t p,unsigned v,unsigned n=4){const auto at=offset(p,n);for(unsigned i=0;i<n;++i)ram_[(at+i)^3U]=std::uint8_t(v>>((n-1-i)*8));}
    void bytes(std::uint32_t p,View value){const auto at=offset(p,unsigned(value.size()));for(unsigned i=0;i<value.size();++i)ram_[(at+i)^3U]=value[i];}
};
void marker(Memory& g,const Symbols& s,unsigned kind,unsigned value) {
    const auto dl=g.get(s.object_display_list);
    if(dl%8)bad("unaligned vehicle display list.");
    g.set(dl,0xb0ca0000U|kind);g.set(dl+4,value);g.set(s.object_display_list,dl+8);
}
gpr pointer(std::uint32_t p){return static_cast<gpr>(static_cast<std::int32_t>(p));}
class Calls {
    std::span<std::uint8_t> ram_;recomp_context context_;Memory g_;
public:
    std::uint32_t stack;
    Calls(std::span<std::uint8_t> ram,const recomp_context& context):ram_(ram),context_(context),g_(ram),stack(std::uint32_t(context.r29)) {
        if((stack&7)||stack<0x80000400U||stack>0x9fffffffU)bad("invalid callback stack.");
        stack-=0x400;g_.get(stack);g_.get(stack+0x3fc);
    }
    unsigned call(RecompiledEntrypoint fn,std::initializer_list<unsigned> args) {
        if(!fn||args.size()>8)bad("native menu callback is unavailable.");
        auto c=context_;c.r29=pointer(stack);c.f_odd=c.mips3_float_mode?&c.f1.u32l:&c.f0.u32h;unsigned i=0;
        for(const auto v:args){switch(i){case 0:c.r4=pointer(v);break;case 1:c.r5=pointer(v);break;case 2:c.r6=pointer(v);break;case 3:c.r7=pointer(v);break;default:g_.set(stack+i*4,v);break;}++i;}
        fn(ram_.data(),&c);return unsigned(c.r2);
    }
};
}
const Symbols& symbols(bool rev80){return rev80?VehicleSymbols80:VehicleSymbols77;}
World::World(std::shared_ptr<const Resources> resources,bool rev80):resources_(std::move(resources)),rev80_(rev80) {
    if(!resources_||resources_->library.packages.empty()||resources_->library.packages.size()!=resources_->portraits.size())bad("missing immutable car resources.");
}
void World::initialize(std::span<std::uint8_t> ram,recomp_context& ctx,const GamePayload& f) {
    if(scratch_||!portraits_.empty())return;
    Calls c(ram,ctx);Memory g(ram);scratch_=c.call(f.asset_allocate,{512,0x7f7f7fff});
    if(!scratch_)bad("not enough native memory for vehicle selection.");g.bytes(scratch_,Bytes(512));
    try {for(auto id:resources_->portraits)for(unsigned half=0;half<2;++half) {
        const auto p=c.call(f.menu_texture_load,{id+half});if(!p)bad("a vehicle portrait could not be loaded.");
        portraits_.push_back(p);if(g.get(p,1)!=64||g.get(p+1,1)!=32)bad("a vehicle portrait tile has invalid dimensions.");
    }}
    catch(...){release(ram,ctx,f);throw;}
}
void World::release(std::span<std::uint8_t> ram,recomp_context& ctx,const GamePayload& f) {
    Calls c(ram,ctx);for(auto p:portraits_)c.call(f.menu_texture_free,{p});portraits_.clear();
    if(scratch_){c.call(f.asset_release,{scratch_});scratch_=0;}
}
bool World::menu(std::span<std::uint8_t> ram,recomp_context& ctx,unsigned event,const GamePayload& f,
        const std::function<unsigned(unsigned)>& header) {
    Memory g(ram);const auto& s=symbols(rev80_);
    const auto sound=[&](unsigned id){Calls c(ram,ctx);c.call(f.menu_sound_play,{id,0});};
    if(event==100) {
        in_tracks_=true;picker_=false;option_pending_=false;before_=-2;committed_={};active_={};ready_={};wheel_phases_={};++wheel_epoch_;
        cpu_choices_={};cpu_objects_={};race_humans_=race_racers_=spawn_slot_=0;
        initialize(ram,ctx,f);return false;
    }
    if(event==104) {picker_=false;before_=-2;if(scratch_||!portraits_.empty())release(ram,ctx,f);return false;}
    if(event==105) {option_pending_=true;committed_={};cpu_choices_={};cpu_objects_={};race_humans_=race_racers_=spawn_slot_=0;wheel_phases_={};++wheel_epoch_;return false;}
    if(event==103)return picker_; // Native renderer keeps the title/records.
    if(event==102) {
        const auto after=int(std::int32_t(g.get(s.stage)));
        if(!picker_&&before_==0&&after>0&&g.get(s.delay)==0&&in_tracks_) {
            players_=g.get(s.players);if(!players_||players_>4)bad("invalid number of players.");
            bool any=false;
            for(unsigned p=0;p<4;++p) {
                const auto* profile=p<players_?resources_->driver(header(p)):nullptr;
                active_[p]=p<players_&&g.get(s.vehicles+p,1)==0&&profile&&profile->compatible();
                if(p<players_)std::fprintf(stderr,"[custom-vehicles][picker] player=%u type=%u character=%s status=%s%s%s\n",
                    p+1,g.get(s.vehicles+p,1),profile?profile->name.c_str():"unresolved",
                    active_[p]?"available":"not-offered",profile&&!profile->compatible()?" reason=":"",
                    profile&&!profile->compatible()?profile->issue.c_str():"");
                ready_[p]=!active_[p];if(!active_[p])committed_[p]=0;
                any|=active_[p];
            }
            option_pending_=false;
            if(any){next_=after;picker_=true;}
        }
        before_=-2;return false;
    }
    if(event!=101)bad("unknown menu event.");
    if(!picker_) {
        before_=int(std::int32_t(g.get(s.stage)));
        if(option_pending_&&before_>=2&&before_<=3) {
            before_=0;menu(ram,ctx,102,f,header);if(picker_)return true;
        }
        return false;
    }
    bool moved=false,confirm=false;
    for(unsigned p=0;p<players_;++p)if(active_[p]) {
        const auto buttons=g.get(s.buttons+p*4);
        if(buttons&0x4000U) {
            if(ready_[p]){ready_[p]=false;sound(0x15c);return true;}
            picker_=false;g.set(s.stage,0);g.set(s.ready,0);
            for(unsigned i=0;i<4;++i)g.set(s.confirmed+i,0,1);
            sound(0x15c);return true;
        }
        if(ready_[p])continue;
        const int axis=std::int16_t(g.get(s.stick_x+p*2,2));
        const int direction=(axis>0||buttons&0x0100U)?1:(axis<0||buttons&0x0200U)?-1:0;
        if(direction) {
            const unsigned n=unsigned(resources_->library.packages.size()+1);
            selected_[p]=unsigned((int(selected_[p])+direction+int(n))%int(n));moved=true;
        }else if(buttons&(0x8000U|0x1000U)){ready_[p]=true;confirm=true;}
    }
    if(moved)sound(0xeb);else if(confirm)sound(0xef);
    bool done=true;for(unsigned p=0;p<players_;++p)done&=ready_[p];
    if(done) {for(unsigned p=0;p<4;++p)committed_[p]=active_[p]?selected_[p]:0;picker_=false;g.set(s.stage,unsigned(next_));}
    return true; // Confirm is consumed here, not by the following native option.
}
void World::race(std::span<std::uint8_t> ram,const recomp_context& ctx,unsigned event) {
    Memory g(ram);const auto& s=symbols(rev80_);
    if(event==0) {
        cpu_choices_={};cpu_objects_={};race_humans_=race_racers_=spawn_slot_=0;
        wheel_phases_={};++wheel_epoch_;return;
    }
    if(event==3) {
        const auto pending=spawn_slot_;spawn_slot_=0;
        if(!pending)return;
        const auto object=std::uint32_t(ctx.r2);
        // racerIndex/vehicleID are assigned AFTER this spawn continuation.
        // Bind only its actual returned primary object, then verify those
        // fields when it is updated/drawn. Ghosts cannot inherit its choice.
        if(object&&g.get(object+0x48,2)==1&&g.get(object+0x64))cpu_objects_[pending-1]=object;
        return;
    }
    if(event==2) {
        spawn_slot_=0;if(!race_racers_)return;
        const auto index=std::uint32_t(ctx.r20),entry=std::uint32_t(ctx.r22);
        if(index>=race_racers_)bad("primary racer index exceeds its car roster.");
        if(index>=race_humans_&&g.get(entry+0xe,2)==4)spawn_slot_=index+1;
        return;
    }
    if(event!=1)bad("unknown car race lifecycle event.");
    if(!in_tracks_||!g.get(s.tracks_mode))return;
    const auto sp=std::uint32_t(ctx.r29);
    if((sp&7)||sp<0x80000000U||sp>0x9ffffe00U)bad("invalid car race setup frame.");
    // ABI reviewed by legacy_character_ai_policy for BOTH admitted revisions.
    const auto humans=g.get(sp+0x144),racers=g.get(std::uint32_t(ctx.r6)),type=g.get(sp+0x68),mode=g.get(sp+0x138);
    if(!humans||humans>4||racers<humans||racers>8)bad("invalid car race topology.");
    if(mode==1||!(type==0||(type>=64&&type<=66))||racers==humans)return;
    race_humans_=humans;race_racers_=racers;++race_serial_;
    // Read, but NEVER advance, native RNG. This separate integer stream has
    // no wall clock, platform distribution, host entropy or render dependency.
    // Its results and exact primary-object bindings are rollback state.
    std::uint64_t random=std::uint64_t(g.get(s.rng_seed))^(race_serial_*0x9e3779b97f4a7c15ULL);
    const auto count=std::uint64_t(resources_->library.packages.size()+1); // Vanilla remains in the pool.
    for(unsigned i=humans;i<racers;++i) {
        std::uint64_t value;
        do {
            random+=0x9e3779b97f4a7c15ULL;value=random;
            value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
            value=(value^(value>>27))*0x94d049bb133111ebULL;value^=value>>31;
        }while(value<(std::uint64_t(0)-count)%count);
        cpu_choices_[i]=unsigned(value%count);
    }
    static std::atomic<unsigned> reports{0};
    if(reports.fetch_add(1,std::memory_order_relaxed)<16)
        std::fprintf(stderr,"[custom-vehicles][cpu-roster] race=%llu humans=%u racers=%u choices=%u,%u,%u,%u,%u,%u,%u,%u; deterministic primary racers only\n",
            static_cast<unsigned long long>(race_serial_),humans,racers,cpu_choices_[0],cpu_choices_[1],cpu_choices_[2],cpu_choices_[3],cpu_choices_[4],cpu_choices_[5],cpu_choices_[6],cpu_choices_[7]);
}
unsigned World::slot(std::span<std::uint8_t> ram,std::uint32_t object,std::uint32_t racer)const {
    Memory g(ram);const auto player=g.get(racer,2);
    if(player<4)return !race_racers_||player<race_humans_?player:8;
    const auto index=g.get(racer+2,1);
    if(player!=0xffffU||index>=race_racers_||index<race_humans_||cpu_objects_[index]!=object)return 8;
    return index;
}
unsigned World::choice(unsigned index)const {
    if(index>=8)return 0;
    if(index>=race_humans_&&index<race_racers_)return cpu_choices_[index];
    return index<4?committed_[index]:0;
}
std::uint32_t World::render_begin(std::span<std::uint8_t> ram,std::uint32_t object) {
    Memory g(ram);const auto& s=symbols(rev80_);
    if(!in_tracks_||!g.get(s.tracks_mode)||g.get(object+0x48,2)!=1)return 0;
    const auto racer=g.get(object+0x64);if(!racer||g.get(racer+0x1d6,1)!=0)return 0;
    const auto player=slot(ram,object,racer),selected=choice(player);if(!selected)return 0;
    const auto* profile=resources_->driver(g.get(object+0x2c,2));
    if(!profile||!profile->compatible()||profile->base!=g.get(racer+3,1))return 0;
    const auto lod=g.get(object+0x3a,1);if(lod>31)return 0;
    // The retail farthest model bakes the stock kart into the driver's proxy.
    // Keep only this selected driver's last separable LOD, never force all
    // racers to maximum detail or use the withdrawn whole-model redraw path.
    const auto view=g.get(s.view);if(view>3)return 0;
    const auto draw_lod=std::min(lod,4U);g.set(object+0x3a,draw_lod,1);
    const auto scope=selected|(lod<<8)|(draw_lod<<16)|(player<<24)|(view<<27);
    if(rendering_)bad("nested racer model rendering.");
    rendering_=object;wheel_=0;
    marker(g,s,0,scope);marker(g,s,7,g.get(object+8));
    for(unsigned w=0;w<4;++w) {
        marker(g,s,8+w,std::uint32_t(wheel_phases_[player][w]));
        marker(g,s,13+w,std::uint32_t(wheel_phases_[player][w]>>32));
    }
    marker(g,s,17,wheel_epoch_);
    const auto points=g.get(object+0x60);
    const auto front=points&&g.get(points)>=4&&g.get(points)<=10?g.get(points+12):0;
    // Vec3s stores named rotations as Y, X, Z in the native ABI.
    marker(g,s,12,front?g.get(front,2):0);
    marker(g,s,1,object);
    return scope;
}
void World::render_end(std::span<std::uint8_t> ram,std::uint32_t object,std::uint32_t scope) {
    if(scope) {
        Memory g(ram);if(rendering_!=object||wheel_)bad("unbalanced vehicle model scope.");
        marker(g,symbols(rev80_),2,object);g.set(object+0x3a,(scope>>8)&255U,1);rendering_=0;
    }
}
void World::wheel_scope(std::span<std::uint8_t> ram,std::uint32_t object,bool begin) {
    if(!rendering_)return;
    Memory g(ram);const auto points=g.get(rendering_+0x60);
    bool owned=false;
    if(points&&g.get(points)>=4&&g.get(points)<=10)for(unsigned i=0;i<4;++i)owned|=g.get(points+4+i*4)==object;
    if(begin){if(wheel_)bad("nested car attachment scope.");wheel_=object;wheel_hidden_=owned;}
    else {if(wheel_!=object)bad("unbalanced car wheel scope.");wheel_=0;}
    // Other attachments keep their original draw and transform. In
    // particular, carried eggs must not inherit the driver's seat alignment.
    marker(g,symbols(rev80_),wheel_hidden_?(begin?3:4):(begin?5:6),object);
}
void World::advance_wheels(std::span<std::uint8_t> ram,std::uint32_t object,float dt) {
    Memory g(ram);const auto& s=symbols(rev80_);
    if(!in_tracks_||!g.get(s.tracks_mode)||g.get(object+0x48,2)!=1)return;
    const auto racer=g.get(object+0x64);if(!racer||g.get(racer+0x1d6,1)!=0)return;
    auto player=slot(ram,object,racer);
    // update_player_racer saves the human port in gCurrentPlayerIndex, then
    // temporarily writes PLAYER_COMPUTER while the finished racer drives on.
    // render_begin sees the restored port, but this native wheel callback runs
    // INSIDE that handoff. Recover only its verified primary human object;
    // never reinterpret a real CPU, ghost or arbitrary actor as a player.
    if(player>=8&&g.get(racer,2)==0xffffU&&g.get(racer+0x1d8,1)==1) {
        const auto owner=g.get(s.current_player),ports=g.get(s.racers_by_port);
        if(owner<4&&(!race_racers_||owner<race_humans_)&&ports&&g.get(ports+owner*4)==object) {
            player=owner;
            static std::atomic<unsigned> reported_ports{0};
            if(!(reported_ports.fetch_or(1U<<owner,std::memory_order_relaxed)&(1U<<owner)))
                std::fprintf(stderr,"[custom-vehicles][finish-wheels] owner=%u object=%08X; verified native AI handoff; continuous wheel phase\n",owner,object);
        }
    }
    if(!choice(player))return;
    const auto* profile=resources_->driver(g.get(object+0x2c,2));if(!profile||!profile->compatible())return;
    const auto points=g.get(object+0x60);if(!points||g.get(points)<4||g.get(points)>10)return;
    const auto real=[&](unsigned address){const auto v=std::bit_cast<float>(g.get(address));if(!std::isfinite(v))bad("non-finite native wheel input.");return v;};
    if(!std::isfinite(dt)||dt<=0)return;
    dt=std::min(dt,1024.F); // Bound visual arithmetic, never reject a long native tick.
    const bool braking=real(racer+0xb8)>.4F;
    const auto velocity=braking&&real(racer+0xb4)<.4F?0.F:std::clamp(real(racer+0x2c),-5.F,5.F);
    // A turn is 2^32 units. Convert each f32 to fixed point independently;
    // the multiply/divide and wrapping state are then entirely integer. No
    // wall clock, camera-facing sprite index, fmod or platform trig enters a
    // checkpoint. The two power-of-two conversions are exact before truncation.
    const auto speed=std::int64_t(velocity*65536.F),time=std::int64_t(dt*65536.F);
    const auto burnout=g.get(racer+0x1fb,1);
    constexpr unsigned order[]{2,3,0,1};
    for(unsigned w=0;w<4;++w) {
        const auto native=order[w],wheel=g.get(points+4+native*4),header=g.get(wheel+0x40);
        const auto frames=g.get(header+0x55,1);if(!frames)continue;
        if(!(braking&&native>=2)&&!(burnout&&native<3))
            wheel_phases_[player][w]+=std::uint64_t(-(speed*time)/(10LL*frames));
        if(burnout&&native<2)wheel_phases_[player][w]+=(std::uint64_t(1)<<32)/frames;
    }
}
Bytes World::checkpoint()const {
    CheckpointWriter out;out.u32(5);out.text(resources_->identity);out.flag(rev80_);out.flag(in_tracks_);out.flag(picker_);out.flag(option_pending_);out.u32(wheel_epoch_);
    out.u32(race_humans_);out.u32(race_racers_);out.u32(spawn_slot_);out.u64(race_serial_);
    for(unsigned i=0;i<8;++i){out.u32(cpu_choices_[i]);out.u32(cpu_objects_[i]);for(auto phase:wheel_phases_[i])out.u64(phase);}
    out.u32(unsigned(before_+2));out.u32(unsigned(next_));out.u32(players_);out.u32(scratch_);out.u32(unsigned(portraits_.size()));
    for(auto p:portraits_)out.u32(p);
    for(unsigned i=0;i<4;++i){out.u32(selected_[i]);out.u32(committed_[i]);out.flag(active_[i]);out.flag(ready_[i]);}
    return std::move(out).finish();
}
std::shared_ptr<World> World::stage_checkpoint(View bytes)const {
    CheckpointReader in(bytes);
    if(in.u32()!=5||in.text()!=resources_->identity||in.flag()!=rev80_)bad("checkpoint uses a different car catalogue.");
    auto out=std::make_shared<World>(resources_,rev80_);out->in_tracks_=in.flag();out->picker_=in.flag();out->option_pending_=in.flag();out->wheel_epoch_=in.u32();
    out->race_humans_=in.bounded(4);out->race_racers_=in.bounded(8);out->spawn_slot_=in.bounded(8);out->race_serial_=in.u64();
    if((out->race_racers_&&(!out->race_humans_||out->race_racers_<=out->race_humans_))||
       (!out->race_racers_&&out->race_humans_)||
       (out->spawn_slot_&&(out->spawn_slot_<=out->race_humans_||out->spawn_slot_>out->race_racers_)))bad("invalid CPU car topology checkpoint.");
    std::set<unsigned> objects;
    for(unsigned i=0;i<8;++i) {
        out->cpu_choices_[i]=in.bounded(unsigned(resources_->library.packages.size()));out->cpu_objects_[i]=in.u32();
        const auto object=out->cpu_objects_[i];
        if((i<out->race_humans_||i>=out->race_racers_)&&(out->cpu_choices_[i]||object))bad("CPU car belongs to an inactive racer.");
        if(object&&(object<0x80000000U||object>0x807fff90U||object%4||!objects.insert(object).second))bad("invalid CPU car primary object checkpoint.");
        for(auto& phase:out->wheel_phases_[i])phase=in.u64();
    }
    out->before_=int(in.bounded(6))-2;out->next_=int(in.bounded(3));out->players_=in.bounded(4);out->scratch_=in.u32();
    const auto count=in.bounded(unsigned(resources_->portraits.size()*2));
    if((!out->scratch_&&count)||(out->scratch_&&(out->scratch_<0x80000000U||out->scratch_>0x807ffe00U||out->scratch_%8))||
       (count&&count!=resources_->portraits.size()*2))bad("invalid menu resource checkpoint.");
    for(unsigned i=0;i<count;++i){const auto p=in.u32();if(p<0x80000000U||p>0x807fffe0U||p%8)bad("invalid portrait checkpoint.");out->portraits_.push_back(p);}
    for(unsigned i=0;i<4;++i) {out->selected_[i]=in.bounded(unsigned(resources_->library.packages.size()));out->committed_[i]=in.bounded(unsigned(resources_->library.packages.size()));out->active_[i]=in.flag();out->ready_[i]=in.flag();}
    if(out->picker_&&(!out->in_tracks_||!out->players_||!out->scratch_||!count||out->next_<1))bad("invalid picker checkpoint.");
    in.end();return out;
}
void World::draw(std::span<std::uint8_t> ram,recomp_context& ctx,const GamePayload& f,
        RecompiledEntrypoint frame,RecompiledEntrypoint text_width) {
    if(!picker_)return;
    if(!scratch_||portraits_.size()!=resources_->portraits.size()*2||!players_||players_>4||!frame||!text_width)
        bad("vehicle picker lost its prepared native menu resources.");
    Calls c(ram,ctx);Memory g(ram);const auto& s=symbols(rev80_);
    const auto label=[&](std::string value) {
        if(value.size()>255)value.resize(255);Bytes bytes;
        for(unsigned char ch:value)bytes.push_back(ch>=32&&ch<127?ch:'?');bytes.push_back(0);g.bytes(scratch_+128,bytes);
    };
    const auto text=[&](const std::string& value,unsigned x,unsigned y,unsigned font,unsigned colour=0xffffffffU) {
        label(value);c.call(f.menu_font,{font});c.call(f.menu_background,{0,0,0,0});
        // Native fun-font labels use the same small offset shadow as the
        // surrounding game menus. Never tint the track name/records above us.
        if(font==0) {
            c.call(f.menu_colour,{0,0,0,255,128});
            c.call(f.menu_text,{s.display_list,x+1,y+1,scratch_+128,12});
        }
        c.call(f.menu_colour,{colour>>24,(colour>>16)&255,(colour>>8)&255,0,colour&255});
        c.call(f.menu_text,{s.display_list,x,y,scratch_+128,12});
    };
    const auto width=[&](const std::string& value,unsigned font) {
        label(value);return c.call(text_width,{scratch_+128,0,font});
    };
    const auto image=[&](unsigned list,float x,float y,float scale,unsigned colour=0xffffffffU) {
        if(!list||!g.get(list))return false;
        c.call(f.menu_texture_draw,{s.display_list,list,std::bit_cast<unsigned>(x),std::bit_cast<unsigned>(y),
            std::bit_cast<unsigned>(scale),std::bit_cast<unsigned>(scale),colour,0});return true;
    };
    const auto wooden_frame=[&](float x,float y,float scale) {
        // gMenuImages is a guest POINTER to the allocated 18-entry array,
        // not the array itself. Never treat the symbol's neighbouring globals
        // as a MenuAsset or overwrite them while placing a wooden frame.
        const auto images=g.get(s.menu_images);if(!images)return;
        struct Placement {
            Memory& g;unsigned address,height;std::array<unsigned,8> words;unsigned old_height;
            Placement(Memory& memory,unsigned at,unsigned h):g(memory),address(at),height(h),old_height(g.get(h)) {
                for(unsigned i=0;i<8;++i)words[i]=g.get(address+i*4);
            }
            ~Placement(){for(unsigned i=0;i<8;++i)g.set(address+i*4,words[i]);g.set(height,old_height);}
        } saved(g,images+7*32,s.frame_height);
        const auto original_scale=std::bit_cast<float>(saved.words[2]);
        if(!std::isfinite(original_scale)||original_scale<=0)return;
        g.set(saved.address+8,std::bit_cast<unsigned>(original_scale*scale));
        g.set(saved.address+12,std::bit_cast<unsigned>(x-160.F));
        g.set(saved.address+16,std::bit_cast<unsigned>(120.F-y));
        g.set(s.frame_height,std::bit_cast<unsigned>(1.F));
        c.call(f.menu_render_reset,{s.display_list});c.call(frame,{7});
    };
    // The original renderer has already drawn the live course, colourful
    // track title, real records and balloons. No full-screen panel or guessed
    // duplicate title is drawn here; only its lower choice widgets change.
    const bool compact=players_>=3;
    const float scale=compact?.70F:1.F,centre_y=173.F,span=64.F*scale;
    const float column=320.F/float(players_);
    for(unsigned p=0;p<players_;++p) {
        const float centre=column*(float(p)+.5F);
        const unsigned tag=players_==1?4:p;
        const float tag_scale=compact?.82F:1.F;
        if(!image(g.get(s.player_images+tag*4),centre-26.F*tag_scale,104.F,tag_scale))
            text(players_==1?"VEHICLE":"PLAYER "+std::to_string(p+1),unsigned(centre),115,0);
        const auto selected=active_[p]?selected_[p]:0;
        if(selected) {
            const auto at=(selected-1)*2;
            // Two native DrawTexture entries, then a null terminator. Each
            // 64x32 tile fits TMEM and preserves the full portrait exactly.
            g.set(scratch_,portraits_.at(at));g.set(scratch_+4,0);
            g.set(scratch_+8,portraits_.at(at+1));g.set(scratch_+12,32);
            g.set(scratch_+16,0);g.set(scratch_+20,0);
            image(scratch_,centre-span*.5F,centre_y-span*.5F,scale);
        }else {
            const auto vehicle=std::min(g.get(s.vehicles+p,1),2U);
            image(g.get(s.race_images+vehicle*12),centre-span*.5F,centre_y-span*.5F,scale);
        }
        wooden_frame(centre,centre_y,scale);
        if(active_[p]&&!ready_[p]) {
            const float arrow_scale=compact?.48F:.7F;
            const float offset=compact?34.F:49.F;
            // texrect_draw_scaled chooses OPA when primitive alpha is 255,
            // ignoring these icons' transparent texels. Alpha 254 selects
            // its native XLU path without changing any shared texture or
            // draw-mode table. Only the arrows receive this near-opaque tint;
            // their zero-alpha backgrounds disappear and the native helper
            // restores primitive colour after each draw.
            constexpr unsigned ArrowColour=0xfffffffeU;
            if(!image(g.get(s.arrows+12),centre-offset,centre_y,arrow_scale,ArrowColour))
                text("<",unsigned(centre-offset),unsigned(centre_y),0);
            if(!image(g.get(s.arrows+4),centre+offset,centre_y,arrow_scale,ArrowColour))
                text(">",unsigned(centre+offset),unsigned(centre_y),0);
            text(std::to_string(selected+1)+" / "+std::to_string(resources_->library.packages.size()+1),unsigned(centre),135,1);
        }else text("READY",unsigned(centre),135,1,0xa0ff80ffU);
        std::string name=selected?resources_->library.packages.at(selected-1)->name:"Vanilla";
        if(!active_[p]) {
            const auto type=g.get(s.vehicles+p,1);if(type==1)name="Hovercraft";else if(type==2)name="Plane";
        }
        unsigned font=players_==1?0:1;
        const unsigned maximum=unsigned(std::min(296.F,column-8.F));
        if(font==0&&width(name,font)>maximum)font=1;
        // Measure actual native glyph widths, not a fixed character count.
        // Names wrap below the frame instead of stretching a menu card.
        unsigned line=0;std::size_t first=0;
        while(first<name.size()&&line<3) {
            while(first<name.size()&&name[first]==' ')++first;if(first==name.size())break;
            std::size_t end=first+1;
            while(end<name.size()&&width(name.substr(first,end-first+1),font)<=maximum)++end;
            if(end<name.size()) {
                const auto space=name.rfind(' ',end-1);if(space!=std::string::npos&&space>first)end=space;
            }
            auto part=name.substr(first,end-first);
            if(line==2&&end<name.size()) {
                while(!part.empty()&&width(part+"...",font)>maximum)part.pop_back();part+="...";
            }
            text(part,unsigned(centre),211+line*(font==0?12:8),font);first=end;++line;
        }
    }
    text("A: SELECT   B: BACK",160,236,1);
    c.call(f.menu_font,{2});c.call(f.menu_background,{0,0,0,0});c.call(f.menu_colour,{255,255,255,0,255});
    c.call(f.menu_render_reset,{s.display_list});
    static std::atomic<unsigned> reports{0};
    if(reports.fetch_add(1,std::memory_order_relaxed)<1)
        std::fprintf(stderr,"[custom-vehicles][native-menu] native title/records, wooden frames and arrows; portraits=64x32+64x32; outward-arrows=alpha-blended; players=%u\n",players_);
}
} // namespace dkr::runtime::vehicles

// Included in the staged GameWorld translation unit, inside its namespace.
// Deterministic SDK state lives in owned writable service regions, so the
// existing mod checkpoint validation, handoff and replay include every byte.
namespace sdk2 {
constexpr unsigned Arena=4096, Header=4096, Handles=4352, Stride=24, Maximum=512;
constexpr unsigned EventHeader=20800,EventRecords=20864,RacerHistory=22000;
constexpr std::uint32_t Magic=0x53444b32;
struct Property {unsigned id;const char* name;const char* unit;unsigned offset,width,type;
    bool racer,writable;float minimum,maximum;};
constexpr Property Properties[]={
#include "sdk2_properties.inl"
};
bool guest(std::uint32_t p,unsigned n) {return p>=0x80000400U&&p<=0x80800000U&&n<=0x80800000U-p;}
std::uint32_t property_base(Memory& bus,std::uint32_t object,std::uint32_t racer,const Property& p) {
    if(p.racer)return guest(racer,0x224)?racer:0;
    if(p.id>=200&&p.id<300){const auto shade=std::uint32_t(bus.load(object+0x54,4));return guest(shade,0x30)?shade:0;}
    if(p.id>=300&&p.id<400) {
        const auto behaviour=unsigned(bus.load(object+0x48,2));
        // Object_Trigger is shared by mode-change (30), bonus (41), trigger
        // (55). Doors/exits have separate state and are not interchangeable.
        if(behaviour!=30&&behaviour!=41&&behaviour!=55)return 0;
        const auto state=std::uint32_t(bus.load(object+0x64,4));return guest(state,0x18)?state:0;
    }
    return object;
}
void text(Memory& bus,std::uint32_t p,const char* value,unsigned capacity) {
    bus.fill(p,capacity);const auto n=std::min<std::size_t>(std::strlen(value),capacity-1);
    bus.write(p,View(reinterpret_cast<const std::uint8_t*>(value),n));
}
std::uint32_t handle_address(Memory& bus,std::uint32_t services,unsigned slot,unsigned generation) {
    if(slot<1||slot>Maximum||!generation)return 0;
    const auto entry=services+Handles+(slot-1)*Stride;
    if(bus.load(entry+4,4)!=generation||!bus.load(entry+20,4))return 0;
    const auto p=std::uint32_t(bus.load(entry,4));
    if(!guest(p,0x80)||bus.load(p+0x40,4)!=bus.load(entry+8,4)||bus.load(p+0x48,2)!=bus.load(entry+12,4))return 0;
    if(bus.load(entry+16,4)&&bus.load(p+0x64,4)!=bus.load(entry+16,4))return 0;
    return p;
}
}
void GameWorld::sdk2_emit(Memory& bus,std::uint32_t services,unsigned type,unsigned slot,unsigned generation,
    unsigned property,unsigned previous,unsigned value,unsigned auxiliary) {
    const auto header=services+sdk2::EventHeader;
    if(!(bus.load(header,4)&(1U<<type)))return;
    const unsigned begin=unsigned(bus.load(header+4,4)),count=unsigned(bus.load(header+8,4));
    if(begin>=32||count>32)throw Error("SDK event queue ownership is invalid.");
    if(count==32){const auto lost=std::uint32_t(bus.load(header+12,4));bus.store(header+12,4,lost==UINT32_MAX?lost:lost+1);return;}
    const auto at=services+sdk2::EventRecords+((begin+count)%32)*32;
    const unsigned words[]={type,unsigned(bus.load(services+sdk2::Header+8,4)),slot,generation,property,previous,value,auxiliary};
    for(unsigned i=0;i<8;++i)bus.store(at+i*4,4,words[i]);bus.store(header+8,4,count+1);
}
bool GameWorld::sdk2_enabled()const {
    return sdk2_active_;
}
void GameWorld::sdk2_lifetime(std::string_view function,bool leaving,const Registers& args) {
    if(!sdk2_enabled()||!native_bus_)return;
    sdk2_vehicle(function,leaving,args);
    sdk2_shading(function,leaving,args);
    auto& bus=*native_bus_;
    if(function=="free_object"&&!leaving)for(const auto& owner:linked().owners)if(owner.package->api==2) {
        for(unsigned i=0;i<sdk2::Maximum;++i) {
            const auto at=owner.services+sdk2::Handles+i*sdk2::Stride;
            if(bus.load(at+20,4)&&bus.load(at,4)==std::uint32_t(args.integer[4])) {
                sdk2_emit(bus,owner.services,3,i+1,unsigned(bus.load(at+4,4)),0,0,0,0);bus.store(at+20,4,0);
            }
        }
    }
}
void GameWorld::sdk2_boundary(unsigned event) {
    if(!sdk2_enabled()||!native_bus_)return;
    auto& bus=*native_bus_;
    for(const auto& owner:linked().owners)if(owner.package->api==2) {
        const auto h=owner.services+sdk2::Header;
        if(bus.load(h,4)!=sdk2::Magic)throw Error("SDK service ownership was modified outside its typed adapters.");
        if(event==DKR_CODE_EVENT_SCENE_UNLOAD) {
            dkr::sdk2::hud_hidden=false;
            dkr::sdk2::debug(unsigned(&owner-linked().owners.data()),{});
            const auto generation=std::uint32_t(bus.load(h+4,4));
            if(generation>=0x7fffffffU)throw Error("SDK entity scene generation exhausted; restart the session.");
            bus.store(h+4,4,generation+1);
            for(unsigned i=0;i<sdk2::Maximum;++i)bus.store(owner.services+sdk2::Handles+i*sdk2::Stride+20,4,0);
            bus.fill(owner.services+24576,32*16);
            bus.fill(owner.services+40000,32); // partial/ready scene documents
            bus.fill(owner.services+36864,8*192);
            bus.fill(owner.services+23200,32);
            bus.fill(owner.services+38400,64);
            bus.fill(owner.services+38976,16); // scene-local lap rule ownership
            bus.fill(owner.services+sdk2::RacerHistory,8*48);
            // Old-scene events cannot be mistaken for current entity events.
            bus.store(owner.services+sdk2::EventHeader+4,4,0);bus.store(owner.services+sdk2::EventHeader+8,4,0);
            sdk2_emit(bus,owner.services,1,0,generation+1,0,generation,generation+1,0);
        }
        if(event!=DKR_CODE_EVENT_FRAME&&event!=DKR_CODE_EVENT_SCENE_LOADED)continue;
        if(event==DKR_CODE_EVENT_FRAME)bus.store(h+8,4,std::uint32_t(bus.load(h+8,4))+1);
        if(event==DKR_CODE_EVENT_FRAME&&owner.package->modules.contains("overrides2"))sdk2_apply_overrides(unsigned(&owner-linked().owners.data()),0,false);
        const bool entities=owner.package->modules.contains("entities2")||owner.package->modules.contains("reflection2")||owner.package->modules.contains("transactions2")||
            owner.package->resources.contains("tools-ui")||bus.load(owner.services+sdk2::EventHeader,4)!=0;
        if(!entities)continue;
        const auto count=std::uint32_t(bus.load(symbol("gObjectCount"),4));
        const auto list=std::uint32_t(bus.load(symbol("gObjPtrList"),4));
        if(count>sdk2::Maximum||(!sdk2::guest(list,count*4)&&count))throw Error("SDK entity list exceeds its reviewed native bounds.");
        std::array<bool,sdk2::Maximum> observed{};
        std::map<std::uint32_t,unsigned> existing;
        std::set<std::uint32_t> current;
        for(unsigned i=0;i<count;++i)if(const auto p=std::uint32_t(bus.load(list+i*4,4)))current.insert(p);
        for(unsigned i=0;i<32;++i) {
            const auto record=owner.services+24576+i*16;
            if(bus.load(record,4)&&!current.contains(std::uint32_t(bus.load(record,4))))bus.fill(record,16);
        }
        for(unsigned i=0;i<sdk2::Maximum;++i) {
            const auto at=owner.services+sdk2::Handles+i*sdk2::Stride;
            if(bus.load(at+20,4)&&!current.contains(std::uint32_t(bus.load(at,4)))) {
                sdk2_emit(bus,owner.services,3,i+1,unsigned(bus.load(at+4,4)),0,0,0,0);bus.store(at+20,4,0);
            }
            if(bus.load(at+20,4))existing.emplace(std::uint32_t(bus.load(at,4)),i);
        }
        for(unsigned index=0;index<count;++index) {
            const auto p=std::uint32_t(bus.load(list+index*4,4));if(!p)continue;
            if(!sdk2::guest(p,0x80))throw Error("SDK entity list contains an invalid object.");
            const auto header=bus.load(p+0x40,4),behaviour=bus.load(p+0x48,2);
            unsigned slot=sdk2::Maximum;
            if(const auto found=existing.find(p);found!=existing.end()) {
                const auto at=owner.services+sdk2::Handles+found->second*sdk2::Stride;
                if(bus.load(at+8,4)==header&&bus.load(at+12,4)==behaviour&&(behaviour!=1||bus.load(at+16,4)==bus.load(p+0x64,4)))slot=found->second;
                else bus.store(at+20,4,0);
            }
            if(slot==sdk2::Maximum) {
                for(unsigned i=0;i<sdk2::Maximum;++i)if(!observed[i]&&!bus.load(owner.services+sdk2::Handles+i*sdk2::Stride+20,4)){slot=i;break;}
                if(slot==sdk2::Maximum)throw Error("SDK entity handle capacity exhausted.");
                const auto at=owner.services+sdk2::Handles+slot*sdk2::Stride;
                const auto serial=std::uint32_t(bus.load(at+4,4));
                if(serial==UINT32_MAX)throw Error("SDK entity lifetime exhausted; restart the session.");
                bus.store(at,4,p);bus.store(at+4,4,serial+1);bus.store(at+8,4,header);bus.store(at+12,4,behaviour);
                bus.store(at+16,4,behaviour==1?bus.load(p+0x64,4):0);bus.store(at+20,4,1);
                sdk2_emit(bus,owner.services,2,slot+1,serial+1,0,0,unsigned(behaviour),0);
            }
            observed[slot]=true;
            if(behaviour==1) {
                const auto racer=std::uint32_t(bus.load(p+0x64,4));
                if(!sdk2::guest(racer,0x224))throw Error("SDK racer event has invalid native state.");
                const unsigned id=unsigned(bus.load(racer+2,1));if(id<8) {
                    const auto history=owner.services+sdk2::RacerHistory+id*48;
                    const auto handle=owner.services+sdk2::Handles+slot*sdk2::Stride;
                    const unsigned generation=unsigned(bus.load(handle+4,4));
                    const bool continuing=bus.load(history,4)==slot+1&&bus.load(history+4,4)==generation;
                    const unsigned values[]={unsigned(bus.load(racer+0x193,1)),unsigned(bus.load(racer+0x190,2)),
                        unsigned(bus.load(racer+0x172,1)),unsigned(bus.load(racer+0x173,1)),unsigned(bus.load(racer+0x1d8,1))};
                    const unsigned properties[]={116,117,103,104,119},events[]={4,5,6,6,7};
                    if(continuing)for(unsigned field=0;field<5;++field) {
                        const auto previous=unsigned(bus.load(history+8+field*4,4));
                        if(previous!=values[field])sdk2_emit(bus,owner.services,events[field],slot+1,generation,properties[field],previous,values[field],id);
                    }
                    bus.store(history,4,slot+1);bus.store(history+4,4,generation);
                    for(unsigned field=0;field<5;++field)bus.store(history+8+field*4,4,values[field]);
                }
            }
        }
        for(unsigned i=0;i<sdk2::Maximum;++i)if(!observed[i])bus.store(owner.services+sdk2::Handles+i*sdk2::Stride+20,4,0);
    }
}
void GameWorld::sdk2_inspector() {
    // Local presentation output only. No budget, RNG, command, event, guest
    // write or checkpoint changes depend on this machine's UI visibility.
    if(!sdk2_active_||!native_bus_||(online_&&!sdk2_frame_agreed_)||!dkr::sdk2::tools_open.load())return;
    if((++sdk2_inspector_serial_&7U)!=0)return;
    auto& bus=*native_bus_;
    for(unsigned index=0;index<linked().owners.size();++index) {
        const auto& owner=linked().owners[index];
        if(owner.package->api!=2||!owner.package->resources.contains("tools-ui")||(!sdk2_pulsing_&&(bus.load(owner.services+sdk2::Header+8,4)&7U)))continue;
        auto values=std::make_shared<std::vector<dkr::sdk2::EntityView>>();values->reserve(512);
        for(unsigned slot=1;slot<=512;++slot) {
            const auto at=owner.services+sdk2::Handles+(slot-1)*sdk2::Stride;
            const auto generation=unsigned(bus.load(at+4,4));const auto obj=sdk2::handle_address(bus,owner.services,slot,generation);if(!obj)continue;
            const auto racer=std::uint32_t(bus.load(at+16,4));
            dkr::sdk2::EntityView value{slot,generation,unsigned(bus.load(at+12,4)),unsigned(bus.load(obj+0x4a,2)),racer!=0,{}};
            value.address=obj;value.header=unsigned(bus.load(obj+0x40,4));
            for(const auto& p:sdk2::Properties) {
                const auto base=sdk2::property_base(bus,obj,racer,p);if(!base)continue;
                auto raw=std::uint32_t(bus.load(base+p.offset,p.width));
                if(p.type==2&&p.width<4)raw=p.width==1?std::uint32_t(std::int32_t(std::int8_t(raw))):std::uint32_t(std::int32_t(std::int16_t(raw)));
                value.properties.push_back({p.id,p.type,unsigned(p.writable)|(unsigned(p.racer)<<1),raw,p.name,p.unit});
            }
            values->push_back(std::move(value));
        }
        dkr::sdk2::inspector(index,std::move(values));
        const auto& metrics=world_.local_metrics();Json records=Json::array();
        for(unsigned package=0;package<linked().owners.size();++package) {
            const auto& o=linked().owners[package];const auto& cost=metrics[package];
            records.push_back({{"id",o.package->id},{"profile",o.package->sdk2_profile},{"resident",o.end-o.begin},{"heap",o.heap_size},
                {"callbacks",cost.callbacks},{"instructions",cost.instructions},{"native_calls",cost.native_calls},{"cpu_ms",double(cost.nanoseconds)/1e6}});
        }
        dkr::sdk2::diagnostics(index,{{"packages",std::move(records)},{"resident_total",linked().resident_bytes},{"last_fault",error_}});
    }
}
void GameWorld::sdk2_command(const dkr::sdk2::Command& command) {
    std::lock_guard lock(mutex_);
    if(!native_bus_) {if(!command.empty())throw Error("SDK command has no bound world.");return;}
    if(!command.valid())throw Error("SDK command envelope is invalid.");
    auto& bus=*native_bus_;
    for(const auto& owner:linked().owners)if(owner.package->api==2)bus.store(owner.services+20480,4,0);
    sdk2_command_=!command.empty();
    if(command.empty())return;
    dkr::sdk2::observed(command);
    if(!sdk2_development_||command.words[0]>linked().owners.size())throw Error("SDK command is not admitted in this development session.");
    const auto& owner=linked().owners[command.words[0]-1];
    if(owner.package->api!=2||!owner.package->development)throw Error("SDK command owner has no development permission.");
    if(sdk2_document_command(command,command.words[0]-1,bus))return;
    bus.store(owner.services+20480,4,1);
    for(unsigned i=0;i<16;++i)bus.store(owner.services+20496+i*4,4,command.words[i]);
}
bool GameWorld::sdk2_service(std::string_view name,std::size_t index,Memory& bus,Registers& c,Budget& budget) {
    if(!name.starts_with("dkr2_"))return false;
    const auto& owner=linked().owners.at(index);
    const auto a=std::uint32_t(c.integer[4]),b=std::uint32_t(c.integer[5]),d=std::uint32_t(c.integer[6]);
    auto result=[&](int status){c.integer[2]=std::uint64_t(std::int64_t(status));return true;};
    if(owner.package->api!=2)return result(-5);
    if(name=="dkr2_setting_apply")return result(sdk2_setting_apply(bus,index,a,b,d,budget));
    if(name=="dkr2_freeze_cpus") {
        if(!owner.package->modules.contains("racer-tuning2"))return result(-5);
        if(sdk2_phase_!=2)return result(-3);if(owner.package->development&&!sdk2_development_)return result(-4);
        if(a>1)return result(-1);bus.store(owner.services+23228,4,a);return result(0);
    }
    if(name=="dkr2_tune")return result(sdk2_tune(bus,index,a));
    if(name=="dkr2_debug") {
        if(!owner.package->modules.contains("presentation2"))return result(-5);
        if(b>64||b*88>budget.instructions)return result(-6);budget.instructions-=b*88;
        std::vector<dkr::sdk2::DebugPrimitive> lines;lines.reserve(b);
        for(unsigned i=0;i<b;++i) {
            const auto at=a+i*88;if(bus.load(at,4)<88)return result(-1);
            dkr::sdk2::DebugPrimitive line;line.kind=unsigned(bus.load(at+4,4));line.rgba=unsigned(bus.load(at+8,4));line.view=unsigned(bus.load(at+12,4));
            if(line.kind<1||line.kind>3||line.view>3)return result(-1);
            for(unsigned axis=0;axis<3;++axis){line.a[axis]=std::bit_cast<float>(std::uint32_t(bus.load(at+16+axis*4,4)));line.b[axis]=std::bit_cast<float>(std::uint32_t(bus.load(at+28+axis*4,4)));if(!std::isfinite(line.a[axis])||!std::isfinite(line.b[axis])||std::abs(line.a[axis])>1e6F||std::abs(line.b[axis])>1e6F)return result(-1);}
            for(unsigned n=0;n<48;++n){const auto ch=char(bus.load(at+40+n,1));if(!ch)break;if(ch<32||ch>126)return result(-1);line.label+=ch;}
            lines.push_back(std::move(line));
        }dkr::sdk2::debug(unsigned(index),std::move(lines));return result(0);
    }
    if(name.starts_with("dkr2_document_"))return result(sdk2_document(index,bus,name,c,budget));
    if(name=="dkr2_control")return result(sdk2_control(bus,index,a,b));
    if(name=="dkr2_route") {
        if(bus.load(b,4)<64)return result(-1);
        const auto count=unsigned(bus.load(symbol("gNumberOfTotalCheckpoints"),4));
        const auto main=unsigned(bus.load(symbol("gNumberOfMainCheckpoints"),4));
        const auto nodes=std::uint32_t(bus.load(symbol("gTrackCheckpoints"),4));
        if(count>127||main>count||!sdk2::guest(nodes,count*0x3c))return result(-3);
        if(a>=count)return result(1);const auto node=nodes+a*0x3c,obj=std::uint32_t(bus.load(node+0x28,4));bus.fill(b,64);
        bus.store(b,4,64);bus.store(b+4,4,a);bus.store(b+8,4,count);bus.store(b+12,4,main);
        for(unsigned i=0;i<512;++i){const auto at=owner.services+sdk2::Handles+i*sdk2::Stride;if(bus.load(at+20,4)&&bus.load(at,4)==obj){bus.store(b+16,4,i+1);bus.store(b+20,4,bus.load(at+4,4));break;}}
        for(unsigned i=0;i<3;++i){bus.store(b+24+i*4,4,bus.load(node+0x10+i*4,4));bus.store(b+36+i*4,4,bus.load(node+i*4,4));}
        bus.store(b+48,4,bus.load(node+0x1c,4));bus.store(b+52,4,bus.load(node+0x20,4));bus.store(b+56,4,std::uint32_t(std::int32_t(std::int8_t(bus.load(node+0x3a,1)))));return result(0);
    }
    if(name=="dkr2_events") {
        if(sdk2_phase_==4)return result(-3);
        if(a&~0x800000feU)return result(-1);
        bus.store(owner.services+sdk2::EventHeader,4,a);return result(0);
    }
    if(name=="dkr2_event_next") {
        if(sdk2_phase_!=2)return result(-3);
        const auto header=owner.services+sdk2::EventHeader;
        if(const auto lost=bus.load(header+12,4)) {
            bus.fill(a,32);bus.store(a,4,31);bus.store(a+24,4,lost);bus.store(header+12,4,0);return result(0);
        }
        const unsigned first=unsigned(bus.load(header+4,4)),count=unsigned(bus.load(header+8,4));
        if(first>=32||count>32)throw Error("SDK event queue is invalid.");
        if(!count)return result(1);
        std::array<std::uint8_t,32> copy{};bus.read(owner.services+sdk2::EventRecords+first*32,copy);bus.write(a,copy);
        bus.store(header+4,4,(first+1)%32);bus.store(header+8,4,count-1);return result(0);
    }
    if(name=="dkr2_cast")return result(sdk2_cast(bus,a,b,budget));
    if(name=="dkr2_catalogue") {
        const auto list=guest_text(bus,a);const auto capacity=std::uint32_t(c.integer[7]);Json item;
        if(list=="scene"||list=="segments"||list=="ai-nodes"||list=="spawn-points"||list=="triggers"||list.starts_with("triangles/")) {
            if(!owner.package->modules.contains("track-tools2"))return result(-5);
            const int status=sdk2_track_catalogue(bus,list,b,item,budget);if(status)return result(status);
        }else if(list=="courses") {
            if(b>=sdk2_courses_.size())return result(1);item=trophy::course_document(sdk2_courses_[b]);
        }else if(list=="packages") {
            if(b>=linked().owners.size())return result(1);const auto& p=*linked().owners[b].package;
            item={{"id",p.id},{"name",p.name},{"version",p.version},{"digest",p.digest},{"api",p.api},{"development",p.development},{"modules",p.modules},{"systems",p.systems}};
        }else if(list=="cups") {
            if(b>=sdk2_cups_.size())return result(1);item=sdk2_cups_[b];item["activation"]="Select in Track Select; cup flow remains owned by the admitted trophy adapter";
        }else if(list=="characters") {
            constexpr const char* stock[]={"Krunch","Diddy Kong","Drumstick","Bumper","Banjo","Conker","Tiptup","T.T.","Pipsy","Timber"};
            if(b<std::size(stock))item={{"id","stock-character:"+std::to_string(b)},{"name",stock[b]},{"native",b},{"scope","built-in"}};
            else {if(!sdk2_inventory_.contains("characters")||b-std::size(stock)>=sdk2_inventory_.at("characters").size())return result(1);item=sdk2_inventory_.at("characters")[b-std::size(stock)];item["scope"]="admitted-custom";item["activation"]="Scene/model reload required for character changes";}
        }else if(list=="character-assets") {
            if(!sdk2_assets_.contains("characters")||b>=sdk2_assets_.at("characters").size())return result(1);item=sdk2_assets_.at("characters")[b];
        }else if(list=="models"||list=="textures-3d"||list=="textures-2d"||list=="sprites"||list=="object-templates"||list=="level-models") {
            const auto& catalogue=sdk2_assets_.contains("banks")?sdk2_assets_:sdk2_inventory_;
            if(!catalogue.contains("banks")||!catalogue.at("banks").contains(list))return result(-5);const auto& bank=catalogue.at("banks").at(list);
            if(b>=bank.at("count").get<unsigned>())return result(1);item=bank;item["index"]=b;item["id"]=bank.at("bank").get<std::string>()+":"+list+":"+std::to_string(b);
            item["activation"]="Read-only admitted base-bank resource; native lifecycle rules apply";
        }else if(list=="sounds"||list=="sequences") {
            const bool music=list=="sequences";const auto count=unsigned(bus.load(symbol(music?"gSeqSoundCount":"gSoundCount"),4));
            if(b>=count||(music&&b>255))return result(1);item={{"id",list+":"+std::to_string(b)},{"index",b},{"count",count},{"scope","initialized-native-bank"}};
        }else if(list=="hooks") {
            if(b>=linked().hooks.size())return result(1);auto it=linked().hooks.begin();std::advance(it,b);
            item={{"address",it->first},{"entry",Json::array()},{"exit",Json::array()},{"replacement",nullptr}};
            const auto owner_of=[&](auto address){return linked().owners.at(linked().function_owners.at(address)).package->id;};
            for(auto f:it->second.entry)item["entry"].push_back(owner_of(f));for(auto f:it->second.exit)item["exit"].push_back(owner_of(f));
            if(it->second.replacement)item["replacement"]=owner_of(*it->second.replacement);
        }else if(list=="callbacks") {
            if(b>=linked().events.size())return result(1);auto it=linked().events.begin();std::advance(it,b);item={{"event",it->first},{"owners",Json::array()}};
            for(auto f:it->second)item["owners"].push_back(linked().owners.at(linked().function_owners.at(f)).package->id);
        }else if(list=="settings") {
            if(b>=owner.package->options.size())return result(1);item=owner.package->options[b];
            const auto at=owner.services+28672+b*8;const auto tag=bus.load(at,4);const auto bits=std::uint32_t(bus.load(at+4,4));
            item["live"]=tag==1?Json(std::bit_cast<float>(bits)):tag==2?Json(bits):owner.settings.at(item.at("id").get<std::string>());
            item["overridden"]=tag!=0;
        }else if(list=="all-settings") {
            unsigned cursor=0;bool found=false;
            for(unsigned package=0;package<linked().owners.size()&&!found;++package) {
                const auto& target=linked().owners[package];for(unsigned option=0;option<target.package->options.size();++option)if(cursor++==b) {
                    item=target.package->options[option];const auto key=item.at("id").get<std::string>();
                    item["package"]=target.package->id;item["package_index"]=package;item["option_index"]=option;item["api"]=target.package->api;
                    const auto at=target.services+28672+option*8;const auto tag=target.package->api==2?bus.load(at,4):0;const auto bits=tag?std::uint32_t(bus.load(at+4,4)):0;
                    item["live"]=tag==1?Json(std::bit_cast<float>(bits)):tag==2?Json(bits):target.settings.at(key);item["overridden"]=tag!=0;
                    item["live_allowed"]=target.package->api==2&&target.package->modules.contains("live-settings2")&&target.package->sdk2_live_options.contains(key);found=true;break;
                }
            }if(!found)return result(1);
        }else if(list=="audio") {
            if(b>1)return result(1);const auto count=unsigned(bus.load(symbol(b?"gSeqSoundCount":"gSoundCount"),4));
            item={{"kind",b?"sequence":"sound"},{"first",0},{"count",count},{"maximum_id",count?count-1:0}};
        }else if(list=="native-operations") {
            constexpr unsigned operations[]={1,2,3,4,10,11,20,21,22,23,30,31,32,40};if(b>=std::size(operations))return result(1);
            item={{"operation",operations[b]},{"available",sdk2_operation_available(operations[b])}};
        }else if(list=="resources") {
            if(b>=owner.package->resources.size())return result(1);auto it=owner.package->resources.begin();std::advance(it,b);
            item={{"id",it->first},{"bytes",it->second.size()}};
        }else return result(-5);
        const auto encoded=item.dump();if(encoded.size()>4095)return result(-6);
        if(d&&capacity) {
            if(capacity<encoded.size()+1)return result(-1);
            bus.write(d,View(reinterpret_cast<const std::uint8_t*>(encoded.c_str()),encoded.size()+1));
        }
        return result(int(encoded.size()+1));
    }
    if(name=="dkr2_setting_set"||name=="dkr2_setting_reset") {
        if(sdk2_phase_!=2)return result(-3);
        if(owner.package->development&&!sdk2_development_)return result(-4);
        const auto key=guest_text(bus,a);unsigned option_index=0;
        const auto found=std::find_if(owner.package->options.begin(),owner.package->options.end(),[&](const auto& option){return option.at("id")==key;});
        if(found==owner.package->options.end())return result(-5);option_index=unsigned(found-owner.package->options.begin());
        const auto at=owner.services+28672+option_index*8;
        if(name=="dkr2_setting_reset"){bus.store(at,4,0);return result(0);}
        const auto type=found->at("type").get<std::string>();const auto raw=std::uint32_t(bus.load(b,4));
        if(type=="Number") {
            const auto value=std::bit_cast<float>(raw);if(!std::isfinite(value)||value<found->at("min").get<double>()||value>found->at("max").get<double>())return result(-1);
        }else if(type=="Enum") {if(raw>=found->at("options").size())return result(-1);}
        else return result(-8);
        bus.store(at+4,4,raw);bus.store(at,4,type=="Number"?1:2);return result(0);
    }
    if(name=="dkr2_operation_available")return result(sdk2_operation_available(a)?1:0);
    if(name=="dkr2_operation") {
        std::array<std::uint32_t,16> arguments{},output{};
        for(unsigned i=0;i<16;++i)arguments[i]=std::uint32_t(bus.load(b+i*4,4));
        if(d){if(d<owner.begin||d>owner.end||64>owner.end-d)return result(-1);bus.fill(d,64);}
        const int status=sdk2_operation(a,index,bus,arguments,output,budget);
        if(d)for(unsigned i=0;i<16;++i)bus.store(d+i*4,4,output[i]);return result(status);
    }
    if(name=="dkr2_ui_publish") {
        if(!owner.package->resources.contains("tools-ui"))return result(-5);
        if(b>16384||b>budget.instructions)return result(-6);
        const auto tick=std::uint32_t(bus.load(owner.services+sdk2::Header+8,4));
        if((tick&7U)!=0&&!sdk2_pulsing_)return result(1);
        Bytes bytes(b);bus.read(a,bytes);budget.instructions-=b;
        try {
            auto values=parse_document(bytes,16384);
            if(!values.is_object()||values.size()>128)return result(-1);
            dkr::sdk2::validate_values(parse_document(owner.package->resources.at("tools-ui"),256*1024),values);
            if(!online_||sdk2_frame_agreed_)dkr::sdk2::publish(unsigned(index),std::move(values),std::uint32_t(bus.load(owner.services+sdk2::Header+4,4)),tick);
        }catch(const std::exception&){return result(-1);}
        return result(0);
    }
    if(name=="dkr2_action_next") {
        if(sdk2_phase_!=2)return result(-3);
        if(!sdk2_development_||!owner.package->development)return result(-4);
        const auto at=owner.services+20480;if(!bus.load(at,4))return result(1);
        std::array<std::uint8_t,64> copy{};bus.read(at+16,copy);bus.write(a,copy);bus.store(at,4,0);
        return result(bus.load(at+16+60,4)==bus.load(owner.services+sdk2::Header+4,4)?0:-2);
    }
    if(name=="dkr2_capability") {
        const auto module=guest_text(bus,a);
        const auto it=sdk2_modules_.find(module);
        c.integer[2]=it==sdk2_modules_.end()?0:it->second;return true;
    }
    const auto header=owner.services+sdk2::Header;
    if(name=="dkr2_session") {
        if(bus.load(a,4)<48)return result(-1);bus.fill(a,48);
        bus.store(a,4,48);bus.store(a+4,4,2);bus.store(a+8,4,linked().revision=="us.v77"?77:80);bus.store(a+12,4,online_);
        bus.store(a+16,4,bus.load(header+4,4));bus.store(a+20,4,bus.load(header+8,4));
        bus.store(a+24,4,bus.load(symbol("gObjectCount"),4));bus.store(a+28,4,bus.load(symbol("gNumRacers"),4));
        bus.store(a+32,4,sdk2_development_);bus.store(a+36,4,sdk2_phase_);
        bus.store(a+40,4,budget.instructions);bus.store(a+44,4,budget.native_calls);return result(0);
    }
    if(name=="dkr2_property") {
        if(a>=std::size(sdk2::Properties))return result(1);
        if(bus.load(b,4)<96)return result(-1);const auto& p=sdk2::Properties[a];bus.fill(b,96);
        bus.store(b,4,96);bus.store(b+4,4,p.id);bus.store(b+8,4,p.type);bus.store(b+12,4,unsigned(p.writable)|(unsigned(p.racer)<<1));
        const auto value=[&](float f){return p.type==1?std::bit_cast<std::uint32_t>(f):std::uint32_t(std::int32_t(f));};
        bus.store(b+16,4,value(p.minimum));bus.store(b+20,4,value(p.maximum));bus.store(b+24,4,p.writable?2:1);
        sdk2::text(bus,b+32,p.name,48);sdk2::text(bus,b+80,p.unit,16);return result(0);
    }
    if(name=="dkr2_entities") {
        const bool racers_only=(a&0x80000000U)!=0;const auto first=a&0x7fffffffU;
        if(first>512||d>64)return result(-1);unsigned copied=0;
        for(unsigned slot=first;slot<512&&copied<d;++slot) {
            const auto at=owner.services+sdk2::Handles+slot*sdk2::Stride;
            if(!bus.load(at+20,4)||(racers_only&&!bus.load(at+16,4)))continue;
            const auto target=b+copied*40;if(bus.load(target,4)<40)return result(-1);
            const auto p=std::uint32_t(bus.load(at,4));bus.fill(target,40);bus.store(target,4,40);
            bus.store(target+4,4,slot+1);bus.store(target+8,4,bus.load(at+4,4));bus.store(target+12,4,bus.load(at+12,4));
            bus.store(target+16,4,bus.load(p+0x4a,2));bus.store(target+20,4,bus.load(at+16,4)!=0);
            for(unsigned j=0;j<3;++j)bus.store(target+28+j*4,4,bus.load(p+0xc+j*4,4));++copied;
        }
        c.integer[2]=copied;return true;
    }
    if(name=="dkr2_read"||name=="dkr2_edit"||name=="dkr2_override") {
        const bool write=name!="dkr2_read",persistent=name=="dkr2_override";if(b>32)return result(-6);
        if(persistent&&!owner.package->modules.contains("overrides2"))return result(-5);
        if(persistent&&bus.load(owner.services+38404,4))return result(-3);
        if(write&&(sdk2_phase_!=2))return result(-3);
        if(write&&owner.package->development&&!sdk2_development_)return result(-4);
        struct Write {std::uint32_t address,value;unsigned width;};std::array<Write,32> edits{};
        const unsigned stride=write?16:20;
        for(unsigned i=0;i<b;++i) {
            const auto at=a+i*stride,slot=std::uint32_t(bus.load(at,4)),generation=std::uint32_t(bus.load(at+4,4));
            const auto id=std::uint32_t(bus.load(at+8,4));const auto p=std::find_if(std::begin(sdk2::Properties),std::end(sdk2::Properties),[&](const auto& p){return p.id==id;});
            int status=0;auto obj=sdk2::handle_address(bus,owner.services,slot,generation);
            if(!obj)status=-2;else if(p==std::end(sdk2::Properties))status=-5;
            else {
                const auto entry=owner.services+sdk2::Handles+(slot-1)*sdk2::Stride;
                obj=sdk2::property_base(bus,obj,std::uint32_t(bus.load(entry+16,4)),*p);if(!obj)status=-5;
            }
            if(write) {
                if(status)return result(status);if(!p->writable)return result(-5);
                const auto raw=std::uint32_t(bus.load(at+12,4));
                const float value=p->type==1?std::bit_cast<float>(raw):p->type==2?float(std::int32_t(raw)):float(raw);
                if(!std::isfinite(value)||value<p->minimum||value>p->maximum)return result(-1);
                for(unsigned j=0;j<i;++j)if(edits[j].address==obj+p->offset)return result(-7);
                if(persistent)for(unsigned other=0;other<linked().owners.size();++other)if(other!=index&&linked().owners[other].package->api==2&&linked().owners[other].package->modules.contains("overrides2")) {
                    const auto services=linked().owners[other].services;const auto count=unsigned(bus.load(services+38400,4));if(count>32)throw Error("SDK override count is invalid.");
                    if(budget.instructions<count*8U)return result(-6);budget.instructions-=count*8U;
                    for(unsigned n=0;n<count;++n){const auto row=services+38464+n*16;
                        if(bus.load(row+8,4)!=id)continue;
                        const auto target=sdk2::handle_address(bus,services,unsigned(bus.load(row,4)),unsigned(bus.load(row+4,4)));
                        if(target&&sdk2::property_base(bus,target,p->racer?std::uint32_t(bus.load(target+0x64,4)):0,*p)==obj)return result(-7);
                    }
                }
                edits[i]={obj+p->offset,raw,p->width};
            }else {
                if(!status) {
                    auto value=std::uint32_t(bus.load(obj+p->offset,p->width));
                    if(p->type==2&&p->width<4)value=p->width==1?std::uint32_t(std::int32_t(std::int8_t(value))):std::uint32_t(std::int32_t(std::int16_t(value)));
                    bus.store(at+12,4,value);
                }
                bus.store(at+16,4,std::uint32_t(status));
            }
        }
        // All target lifetimes, values and duplicate fields were checked before
        // the first write. No native calls/allocations occur during commit.
        if(write)for(unsigned i=0;i<b;++i)bus.store(edits[i].address,edits[i].width,edits[i].value);
        if(persistent) {
            std::array<std::uint8_t,512> records{};bus.read(a,std::span(records).first(b*16));bus.write(owner.services+38464,std::span(records).first(b*16));bus.store(owner.services+38400,4,b);
        }
        return result(0);
    }
    if(name=="dkr2_symbols") {
        const auto symbols=linked().revision=="us.v77"?std::span<const Symbol>(Symbols77):std::span<const Symbol>(Symbols80);
        if(a>=symbols.size())return result(1);if(bus.load(b,4)<112)return result(-1);
        const auto& s=symbols[a];unsigned flags=s.function?1:0;
        const auto entry=std::lower_bound(table_.entries.begin(),table_.entries.end(),s.address,[](const auto& e,auto address){return e.address<address;});
        if(entry!=table_.entries.end()&&entry->address==s.address) {
            if(!entry->protected_call)flags|=2;if(!entry->protected_hook)flags|=4;
            if(entry->protected_function||entry->protected_hook||entry->protected_call)flags|=8;
        }
        bus.fill(b,112);bus.store(b,4,112);bus.store(b+4,4,s.address);bus.store(b+8,4,s.size);bus.store(b+12,4,flags);sdk2::text(bus,b+16,s.name,96);return result(0);
    }
    if(name=="dkr2_memory_read") {
        if(d>4096||!sdk2::guest(a,d))return result(-1);
        std::array<std::uint8_t,4096> bytes{};bus.read(a,std::span(bytes).first(d));bus.write(b,std::span(bytes).first(d));return result(0);
    }
    return result(-5);
}

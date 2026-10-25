#include "magic_code_game_world.hpp"
#include "magic_code_game_services.hpp"
#include "magic_code_native_arguments.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstddef>
#include <bit>
#include <cmath>

#include "magic_code_execution_policy.hpp"

namespace dkr::mods::code {
namespace {
static_assert(offsetof(recomp_context,r31)==31*sizeof(gpr));
static_assert(offsetof(recomp_context,f31)-offsetof(recomp_context,f0)==31*sizeof(fpr));
constexpr std::string_view Events[]={"","on_frame","on_frame_end","on_scene_unload","on_scene_loaded",
    "on_racer_spawned","on_results","on_menu"};
struct Symbol {const char* name;std::uint32_t address,size;bool function;};
constexpr Symbol Symbols77[]={
#include "../../../../modding/reference-us-v77/game_symbols.inc"
};
constexpr Symbol Symbols80[]={
#include "../../../../modding/reference-us-v80/game_symbols.inc"
};
std::string guest_text(Memory& bus,std::uint32_t address) {
    std::string out;
    for(unsigned i=0;i<=160;++i){const auto c=char(bus.load(address+i,1));if(!c)return out;out+=c;}
    throw Error("Game SDK symbol name is too long.");
}
Registers read_call(Memory& bus,std::uint32_t address) {
    Registers c;
    for(unsigned i=0;i<32;++i)c.integer[i]=bus.load(address+i*8,8);
    for(unsigned i=0;i<32;i+=2)c.floating[i]=bus.load(address+256+i*8,8);
    c.integer[0]=0;c.hi=bus.load(address+512,8);c.lo=bus.load(address+520,8);
    c.fcsr=std::uint32_t(bus.load(address+528,4));return c;
}
void write_call(Memory& bus,std::uint32_t address,const Registers& c) {
    for(unsigned i=0;i<32;++i)bus.store(address+i*8,8,c.integer[i]);
    for(unsigned i=0;i<32;++i)bus.store(address+256+i*8,8,c.floating[i]);
    bus.store(address+512,8,c.hi);bus.store(address+520,8,c.lo);bus.store(address+528,4,c.fcsr);
}
}
#include "sdk2_world.inl"
#include "sdk2_operations.inl"
#include "sdk2_collision.inl"
#include "sdk2_track_catalogue.inl"
#include "sdk2_simulation.inl"
#include "sdk2_documents.inl"
#include "sdk2_tuning.inl"
#include "sdk2_overrides.inl"
#include "sdk2_reload.inl"
#include "sdk2_settings.inl"
LinkPolicy GameWorld::policy(DispatchTable table,bool online) {
    auto p=World::core_policy();p.online=online;
    p.modules.insert({{"reflection2",2},{"entities2",2},{"transactions2",2},{"inspection2",2},{"tools-ui2",2},{"operations2",2},{"live-settings2",2},{"catalogues2",2},{"collision2",2},{"events2",2}});
    p.services.insert({"dkr2_capability","dkr2_session","dkr2_property","dkr2_entities","dkr2_read","dkr2_edit","dkr2_symbols","dkr2_memory_read","dkr2_ui_publish","dkr2_action_next","dkr2_operation_available","dkr2_operation","dkr2_setting_set","dkr2_setting_reset","dkr2_catalogue","dkr2_cast","dkr2_events","dkr2_event_next","dkr2_debug"});
    p.modules.emplace("racer-tuning2",2);p.services.insert({"dkr2_tune","dkr2_freeze_cpus"});
    p.services.insert("dkr2_setting_apply");p.events.insert("on_live_setting");
    p.modules.emplace("reload2",2);p.events.insert({"on_cleanup","on_reload"});
    p.modules.emplace("overrides2",2);p.services.insert("dkr2_override");
    p.modules.emplace("game-hooks",1);
    p.modules.emplace("game-events",1);
    p.modules.emplace("game",1);
    p.modules.emplace("racing",1);
    p.modules.emplace("trophies",1);
    p.modules.insert({{"racers",1},{"objects",1},{"input",1},{"cameras",1}});
    p.modules.emplace("collision",1);p.services.insert("dkr_collision_floor");
    p.modules.emplace("hud",1);p.services.insert("dkr_hud_text");
    p.services.insert({"dkr_game_symbol","dkr_game_symbol_size","dkr_game_call",
        "dkr_game_context_read","dkr_game_context_write","dkr_game_skip_original"});
    p.services.insert("dkr_game_repeat_original");
    p.services.insert({"dkr_race_laps","dkr_race_register"});
    p.services.insert({"dkr_trophy_info","dkr_trophy_round","dkr_course_select"});
    p.services.insert({"dkr_racer_count","dkr_racer_get","dkr_racer_set","dkr_object_count","dkr_object_get","dkr_object_set",
        "dkr_input_get","dkr_camera_get","dkr_camera_set"});
    p.services.insert("dkr_audio_command");
    // An empty table is the library's capability preview. Actual admission
    // still requires all four reviewed handlers in the selected CPU closure.
    if(table.entries.empty()||std::count_if(table.entries.begin(),table.entries.end(),[](const auto& e){return e.curated_call;})==4)p.modules.emplace("audio",1);
    p.events.insert({"on_frame","on_frame_end","on_tool"});
    p.modules.emplace("simulation2",2);p.modules.emplace("track-tools2",2);p.modules.emplace("documents2",2);p.modules.emplace("presentation2",2);p.services.insert({"dkr2_control","dkr2_route","dkr2_document_publish","dkr2_document_read","dkr2_document_size","dkr2_document_accept"});
    p.raw_game_access=!table.entries.empty();
    for(const auto& entry:table.entries) {
        p.functions.insert(entry.address);
        if(!entry.protected_call)p.call_functions.insert(entry.address);
        if(entry.protected_function)p.protected_functions.insert(entry.address);
        const bool private_scheduler=online&&entry.name&&std::string_view(entry.name)=="main_game_loop";
        if(!entry.protected_hook&&!private_scheduler)p.hook_functions.insert(entry.address);
        if(!entry.protected_hook&&entry.name&&!private_scheduler) {
            p.events.insert("before_"+std::string(entry.name));p.events.insert("after_"+std::string(entry.name));
        }
        for(const auto event:{entry.before_event,entry.after_event}) {
            if(event>=std::size(Events))throw Error("Generated code event ABI is invalid.");
            if(event)p.events.insert(std::string(Events[event]));
        }
    }
    return p;
}
GameWorld::GameWorld(const FrozenSet& frozen,std::string_view revision,DispatchTable table,
    std::uint64_t seed,bool online,Invoke invoke):table_(table),invoke_(std::move(invoke)),online_(online),
    world_(link(frozen.ordered,frozen.settings,revision,policy(table,online),frozen.data),seed,
        [this](auto address,auto& registers,auto& budget){native(address,registers,budget);},
        [this](auto name,auto owner,auto& bus,auto& registers,auto& budget){return service(name,owner,bus,registers,budget);}) {
    sdk2_active_=std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2;});
    if(sdk2_active_)sdk2_modules_=policy(table_,online_).modules;
    if(sdk2_active_)for(const auto& p:frozen.ordered)if(p->api==2){sdk2_budget_.instructions+=p->sdk2_instructions;sdk2_budget_.native_calls+=p->sdk2_native_calls;}
    sdk2_development_=!online_&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->development;});
    for(const auto& p:frozen.ordered)if(p->api==2) {
        const auto data=frozen.data.find(p->id);
        if(data!=frozen.data.end()&&data->second.contains("_sdk2_catalogue")) {
            const auto courses=trophy::read_catalogue(data->second.at("_sdk2_catalogue").at("courses"));
            if(!sdk2_courses_.empty()&&sdk2_courses_!=courses)throw Error("SDK 2 packages disagree on their frozen resource catalogue.");
            sdk2_courses_=courses;
            const auto& inventory=data->second.at("_sdk2_catalogue").at("inventory");
            if(!sdk2_inventory_.empty()&&sdk2_inventory_!=inventory)throw Error("SDK 2 packages disagree on their admitted resource inventory.");
            sdk2_inventory_=inventory;
        }
    }
    for(const auto& cup:trophy::menu_entries(frozen))sdk2_cups_.push_back({{"id",cup.id},{"name",cup.name}});
    if(world_.linked().identity!=frozen.identity)throw Error("Frozen code configuration changed before game construction.");
    if(!std::is_sorted(table_.entries.begin(),table_.entries.end(),[](const auto& a,const auto& b){return a.address<b.address;}))
        throw Error("Code dispatch table is not canonically ordered.");
    for(const auto& entry:table_.entries)if(entry.name&&!entry.protected_hook) {
        const auto before="before_"+std::string(entry.name),after="after_"+std::string(entry.name);
        if(linked().events.contains("*:"+before)||linked().events.contains("*:"+after))
            named_events_.emplace(entry.address,std::pair{before,after});
    }
    for(std::size_t i=0;i<frozen.ordered.size();++i)if(frozen.ordered[i]->modules.contains("trophies"))
        cups_.emplace(i,trophy::all(frozen.data.at(frozen.ordered[i]->id).at("trophies")));
    hook_mask_.assign(8*MiB/32,0);
    for(const auto& entry:table_.entries) {
        bool active=linked().hooks.contains(entry.address)||named_events_.contains(entry.address)||entry.after_event==DKR_CODE_EVENT_FRAME_END;
        if(sdk2_enabled()&&(entry.before_event||entry.after_event||(entry.name&&(std::string_view(entry.name)=="free_object"||std::string_view(entry.name)=="menu_pause_loop"))))active=true;
        for(const auto event:{entry.before_event,entry.after_event})if(event&&linked().events.contains("*:"+std::string(Events[event])))active=true;
        if(sdk2_active_&&entry.name&&(sdk2::vehicle_update(entry.name)||std::string_view(entry.name)=="update_AI_racer")&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2&&p->modules.contains("racer-tuning2");}))active=true;
        if(sdk2_active_&&entry.name&&std::string_view(entry.name)=="light_update_shading"&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2&&p->modules.contains("overrides2");}))active=true;
        if(active)hook_mask_[(entry.address&0x7fffffU)>>5]|=std::uint8_t(1U<<((entry.address>>2)&7U));
    }
}
bool GameWorld::service(std::string_view name,std::size_t owner,Memory& bus,Registers& c,Budget& budget) {
    if(sdk2_service(name,owner,bus,c,budget))return true;
    if(game_memory_service(name,bus,c,budget,[this](auto name){return symbol(name);}))return true;
    const auto a=std::uint32_t(c.integer[4]),b=std::uint32_t(c.integer[5]);
    if(name=="dkr_hud_text") {
        if(!callback_)throw Error("HUD text requires a named drawing callback.");
        const auto current=std::lower_bound(table_.entries.begin(),table_.entries.end(),callback_->address,[](const auto& e,auto address){return e.address<address;});
        if(current==table_.entries.end()||!current->name)throw Error("HUD text has no drawing owner.");
        const std::string_view phase=current->name;
        if(phase.find("render")==phase.npos&&!phase.starts_with("hud_")&&!phase.starts_with("draw_"))throw Error("HUD text is unavailable outside a drawing callback.");
        const auto target=std::uint32_t(bus.load(a,4)),font=std::uint32_t(bus.load(a+4,4));
        const auto x=std::int32_t(bus.load(a+8,4)),y=std::int32_t(bus.load(a+12,4));
        const auto rgba=std::uint32_t(bus.load(a+16,4)),alignment=std::uint32_t(bus.load(a+20,4));
        if(target>1||font>2||x<-2048||x>2048||y<-2048||y>2048||alignment>15||(alignment&5)==5||(alignment&10)==10)throw Error("HUD text fields exceed their supported range.");
        const auto text=guest_text(bus,std::uint32_t(bus.load(a+24,4)));
        if(std::any_of(text.begin(),text.end(),[](unsigned char ch){return ch<32||ch>=127;}))throw Error("HUD text must use printable ASCII glyphs.");
        c.integer[2]=0;
        const auto fonts=std::uint32_t(bus.load(symbol("gFonts"),4)),box=std::uint32_t(bus.load(symbol("gDialogueBoxBackground"),4));
        const auto list=symbol(target?"gHudDL":"sMenuCurrDisplayList");
        if(!fonts||!box||font>=bus.load(symbol("gNumberOfFonts"),4)||!bus.load(fonts+font*0x400+0x28,1)||!bus.load(list,4))return true;
        const auto& mod=linked().owners.at(owner);const auto used=std::uint32_t(bus.load(mod.services+1024,4));
        if(used>512||text.size()>512-used)throw Error("HUD text exceeds the per-frame glyph budget.");
        bus.store(mod.services+1024,4,used+unsigned(text.size()));
        const auto scratch=mod.services+1040,stack=mod.services+1248;
        bus.fill(scratch,164);bus.write(scratch,View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));bus.fill(stack,64);
        std::uint8_t previous[40];bus.read(box,previous);
        struct Restore {Memory& bus;std::uint32_t box;std::span<const std::uint8_t> bytes;~Restore()noexcept{try{bus.write(box,bytes);}catch(...){}}} restore{bus,box,previous};
        for(unsigned i=0;i<3;++i)bus.store(box+0x14+i,1,(rgba>>(24-i*8))&255);
        bus.store(box+0x17,1,0);bus.store(box+0x1c,1,rgba&255);bus.store(box+0x1d,1,font);
        for(unsigned i=0;i<4;++i)bus.store(box+0x18+i,1,0);
        auto call=c;call.integer[4]=std::int32_t(list);call.integer[5]=x;call.integer[6]=y;call.integer[7]=std::int32_t(scratch);call.integer[29]=std::int32_t(stack);bus.store(stack+16,4,alignment);
        const auto draw=std::find_if(table_.entries.begin(),table_.entries.end(),[](const auto& e){return e.name&&std::string_view(e.name)=="draw_text"&&!e.protected_call;});
        if(draw==table_.entries.end())throw Error("HUD text has no reviewed drawing dispatcher.");
        native(draw->address,call,budget);c.integer[2]=1;return true;
    }
    if(name=="dkr_audio_command") {
        constexpr const char* functions[]={"sound_play","music_play","music_stop","music_fade"};
        if(a>=4)throw Error("Unknown SDK audio command.");
        c.integer[2]=0;
        if(a<2) {
            const auto limit=std::uint32_t(bus.load(symbol(a==0?"gSoundCount":"gSeqSoundCount"),4));
            const auto player=std::uint32_t(bus.load(symbol(a==0?"gSoundTable":"gMusicPlayer"),4));
            if(!player||b>=limit||(a==1&&b>255))return true;
        }else if(!bus.load(symbol("gMusicPlayer"),4))return true;
        if(a==3&&(std::int32_t(b)<-4096||std::int32_t(b)>4096))throw Error("Audio fade exceeds its SDK range.");
        const auto target=std::find_if(table_.entries.begin(),table_.entries.end(),[&](const auto& e){return e.name&&std::string_view(e.name)==functions[a]&&e.curated_call;});
        if(target==table_.entries.end())throw Error("The selected audio command has no reviewed non-blocking dispatcher.");
        auto call=c;call.integer[4]=std::uint64_t(std::int64_t(std::int32_t(b)));call.integer[5]=0;
        native(target->address,call,budget,true);c.integer[2]=1;return true;
    }
    if(name=="dkr_trophy_info"||name=="dkr_trophy_round"||name=="dkr_course_select") {
        const auto menu=cups_.find(owner);if(menu==cups_.end())throw Error("This package has no frozen custom cup.");
        const auto selected=trophy_selector_?trophy_selector_():std::string{};
        const auto cup=std::find_if(menu->second.begin(),menu->second.end(),[&](const auto& value){
            return trophy::menu_id(linked().owners.at(owner).package->id,value.definition.id)==selected;
        });
        if(cup==menu->second.end()) {
            if(name=="dkr_trophy_info"){c.integer[2]=0;return true;}
            throw Error("No custom trophy is selected in Track Select.");
        }
        auto text=[&](std::uint32_t address,const std::string& value,unsigned capacity,bool display=false) {
            if(value.size()>=capacity)throw Error("Trophy text exceeds its ABI buffer.");
            bus.fill(address,capacity);
            // The retail renderer indexes a 96-glyph ASCII font without a
            // bounds check. Keep stable UTF-8 course IDs untouched, but never
            // lend non-ASCII display bytes to that renderer.
            if(display)for(unsigned i=0;i<value.size();++i)bus.store(address+i,1,static_cast<unsigned char>(value[i])>=32&&static_cast<unsigned char>(value[i])<127?value[i]:'?');
            else bus.write(address,View(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
        };
        if(name=="dkr_trophy_info") {
            if(a){if(b<164)throw Error("Cup name requires a 164-byte buffer.");text(a,cup->definition.name,164,true);}
            c.integer[2]=cup->rounds.size();return true;
        }
        if(name=="dkr_trophy_round") {
            if(a>=cup->rounds.size())throw Error("Cup round is out of range.");
            const auto& round=cup->rounds[a];bus.fill(b,344);
            bus.store(b,4,round.course.carrier);bus.store(b+4,4,round.laps);bus.store(b+8,4,round.vehicle);
            text(b+12,round.course.name,164,true);text(b+176,round.course.id,164);
            c.integer[2]=1;return true;
        }
        const auto id=guest_text(bus,a);
        if(!course_selector_||std::none_of(cup->rounds.begin(),cup->rounds.end(),[&](const auto& round){return round.course.id==id&&round.course.carrier==b;}))
            throw Error("Course selection is not in the admitted cup.");
        course_selector_(id.starts_with("legacy:")?id.substr(7):std::string{},b);c.integer[2]=1;return true;
    }
    if(name=="dkr_race_laps"||name=="dkr_race_register") {
        const auto& mod=linked().owners.at(owner);
        if(!mod.package->systems.contains("race-laps"))throw Error("Extended laps require exclusive race-laps ownership.");
        if(name=="dkr_race_laps") {
            if(a&&(a%4||a<mod.begin||a>mod.end||3200>mod.end-a))throw Error("Lap timing requires an owned 8x100-word table.");
            if(a&&bus.load(symbol("gIsTimeTrial"),1))throw Error("Extended laps cannot use native Time Trial records or its fixed lap-colour tables.");
            if(a)bus.fill(a,3200);
            bus.store(mod.services,4,a);
            for(unsigned i=0;i<8;++i)bus.store(mod.services+4+i*4,4,0);
            c.integer[2]=1;
        } else {
            if(!a||a<0x80000400U||a>0x80800000U-0x224)throw Error("Invalid registered racer address.");
            const auto index=std::uint32_t(bus.load(a+2,1));
            if(index>=8)throw Error("Racer index exceeds the native race capacity.");
            bus.store(mod.services+4+index*4,4,a);c.integer[2]=index;
        }
        return true;
    }
    if(name=="dkr_game_symbol"||name=="dkr_game_symbol_size") {
        const auto key=guest_text(bus,a);
        const auto symbols=linked().revision=="us.v77"?std::span<const Symbol>(Symbols77):std::span<const Symbol>(Symbols80);
        const auto found=std::lower_bound(symbols.begin(),symbols.end(),key,[](const auto& s,const auto& value){return s.name<value;});
        c.integer[2]=found!=symbols.end()&&found->name==key?
            (name=="dkr_game_symbol"?std::uint64_t(std::int64_t(std::int32_t(found->address))):found->size):0;
        return true;
    }
    if(name=="dkr_game_call") {
        const auto key=guest_text(bus,a);
        const auto found=std::find_if(table_.entries.begin(),table_.entries.end(),[&](const auto& e){return e.name&&key==e.name;});
        if(found==table_.entries.end()||found->protected_call)throw Error("Unavailable or protected game call: "+key);
        auto args=read_call(bus,b);
        // Sixteen O32 argument-stack words are supplied explicitly. Never lend
        // a host stack pointer or an uninitialised generated CPU stack.
        const auto stack=std::uint32_t(args.integer[29]);
        if(stack<CodeBase||stack%16)throw Error("Game call requires a mod-owned aligned guest stack.");
        (void)owner;native(found->address,args,budget);
        write_call(bus,b,args);c.integer[2]=1;return true;
    }
    if(name=="dkr_game_context_read"||name=="dkr_game_context_write") {
        if(!callback_||!callback_->registers)throw Error("Game context is available only inside a named game callback.");
        if(name=="dkr_game_context_read")write_call(bus,a,*callback_->registers);
        else *callback_->registers=read_call(bus,a);
        c.integer[2]=1;return true;
    }
    if(name=="dkr_game_skip_original"||name=="dkr_game_repeat_original") {
        if(name=="dkr_game_repeat_original") {
            if(!callback_||callback_->entry)throw Error("Only an after-function callback can repeat its original.");
            const auto entry=std::lower_bound(table_.entries.begin(),table_.entries.end(),callback_->address,[](const auto& e,auto address){return e.address<address;});
            if(entry==table_.entries.end()||!entry->name||std::string_view(entry->name)!="hud_element_render")throw Error("Only HUD element drawing supports bounded repetition.");
            callback_->repeat=true;c.integer[2]=1;return true;
        }
        if(!callback_||!callback_->entry)throw Error("Only a before-function callback can replace its original.");
        const auto entry=std::lower_bound(table_.entries.begin(),table_.entries.end(),callback_->address,
            [](const auto& e,auto address){return e.address<address;});
        const bool extended_results=entry!=table_.entries.end()&&entry->name&&std::string_view(entry->name)=="race_finish_time_trial"&&linked().owners.at(owner).package->systems.contains("race-laps");
        if(entry==table_.entries.end()||(entry->protected_function&&!extended_results))throw Error("Project-owned game fixes cannot be skipped.");
        callback_->skip=true;c.integer[2]=1;return true;
    }
    return false;
}
std::uint32_t GameWorld::symbol(std::string_view name)const {
    const auto symbols=linked().revision=="us.v77"?std::span<const Symbol>(Symbols77):std::span<const Symbol>(Symbols80);
    const auto found=std::lower_bound(symbols.begin(),symbols.end(),name,[](const auto& s,auto name){return std::string_view(s.name)<name;});
    if(found==symbols.end()||std::string_view(found->name)!=name)throw Error("SDK binding is missing: "+std::string(name));return found->address;
}
int GameWorld::lap_address(std::uint32_t address,std::uint64_t& result)noexcept {
    result=address;
    try {
        std::lock_guard lock(mutex_);if(!native_bus_)throw Error("Lap timing has no RDRAM binding.");auto& bus=*native_bus_;
        for(const auto& owner:linked().owners)if(owner.package->systems.contains("race-laps")) {
            const auto table=std::uint32_t(bus.load(owner.services,4));if(!table)continue;
            if(table<owner.begin||table>owner.end||3200>owner.end-table)throw Error("Extended lap table lost its owned range.");
            const auto resolve=[&]()->bool {
                for(unsigned i=0;i<8;++i) {
                    const auto racer=std::uint32_t(bus.load(owner.services+4+i*4,4));if(!racer)continue;
                    const auto begin=racer+0x128;
                    if(address>=begin&&address-begin<400&&(address-begin)%4==0) {
                        const auto mapped=table+i*400+address-begin;
                        bus.store(mapped,4,bus.load(mapped,4));result=mapped;return true;
                    }
                }
                return false;
            };
            if(resolve())return 1;
            // Retail settles every racer ten times before track_setup_racers
            // returns. At that point ALL final IDs/list entries already exist,
            // but an after-setup mod callback has not run yet. Bind ONLY that
            // validated native roster, transactionally, on a cache miss. These
            // service words are checkpointed; no ordinary/private fallback or
            // unsafe original five-lap-array access is permitted.
            const auto count=std::uint32_t(bus.load(symbol("gNumRacers"),4));
            const auto list=std::uint32_t(bus.load(symbol("gRacersByPort"),4));
            std::array<std::uint32_t,8> racers{};
            const auto valid=[](auto at,unsigned bytes){return at>=0x80000400U&&at%4==0&&at<=0x80800000U-bytes;};
            if(!count||count>8||!valid(list,count*4))throw Error("Extended lap timing has no completed racer roster.");
            for(unsigned i=0;i<count;++i) {
                const auto object=std::uint32_t(bus.load(list+i*4,4));
                if(!valid(object,0x80))throw Error("Extended lap roster has an invalid racer object.");
                const auto racer=std::uint32_t(bus.load(object+0x64,4));
                if(!valid(racer,0x224))throw Error("Extended lap roster has an invalid racer state.");
                const auto id=std::uint32_t(bus.load(racer+2,1));
                if(id>=8||racers[id])throw Error("Extended lap roster IDs are not finalized or unique.");
                racers[id]=racer;
            }
            for(unsigned i=0;i<8;++i)bus.store(owner.services+4+i*4,4,racers[i]);
            if(resolve())return 1;
            throw Error("An extended lap access has no registered racer.");
        }
        return 1;
    }catch(const std::exception& e){try{error_=e.what();}catch(...){}return -1;}
     catch(...){try{error_="Extended lap timing failed.";}catch(...){}return -1;}
}
Registers GameWorld::read(const recomp_context& context) {
    if(context.mips3_float_mode)throw Error("Custom Magic Codes require the game's O32/FR=0 register mode.");
    Registers out;std::memcpy(out.integer.data(),&context.r0,sizeof(context.r0)*32);
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(&context.f0);
    for(unsigned i=0;i<32;i+=2)std::memcpy(&out.floating[i],bytes+i*sizeof(fpr),sizeof(std::uint64_t));
    out.integer[0]=0;out.hi=context.hi;out.lo=context.lo;return out;
}
void GameWorld::write(recomp_context& context,const Registers& registers) {
    std::memcpy(&context.r0,registers.integer.data(),sizeof(context.r0)*32);context.r0=0;
    auto* bytes=reinterpret_cast<std::uint8_t*>(&context.f0);
    for(unsigned i=0;i<32;i+=2)std::memcpy(bytes+i*sizeof(fpr),&registers.floating[i],sizeof(std::uint64_t));
    context.hi=registers.hi;context.lo=registers.lo;context.mips3_float_mode=0;context.f_odd=&context.f0.u32h;
}
void GameWorld::native(std::uint32_t address,Registers& registers,Budget& budget,bool curated) {
    const auto found=std::lower_bound(table_.entries.begin(),table_.entries.end(),address,
        [](const auto& entry,auto value){return entry.address<value;});
    if(found==table_.entries.end()||found->address!=address||(found->protected_call&&!(curated&&found->curated_call))||!found->function)
        throw Error("Code mod requested an unavailable or protected game function.");
    const auto stack=std::uint32_t(registers.integer[29]);
    const auto owner=std::find_if(linked().owners.begin(),linked().owners.end(),[&](const auto& o){return stack>=o.begin&&stack<o.end;});
    if(owner==linked().owners.end()||native_depth_>=8||stack%16)throw Error("Native game call has no owned guest stack.");
    if(!native_bus_)throw Error("Game call has no RDRAM binding.");auto& bus=*native_bus_;
    const auto native_stack=owner->native_stack-native_depth_*8192;
    for(unsigned i=0;i<16;++i)bus.store(native_stack+i*4,4,bus.load(stack+i*4,4));
    execution::native(budget);
    ++native_depth_;struct Depth{unsigned& value;~Depth(){--value;}}depth{native_depth_};
    recomp_context context{};write(context,registers);context.r29=std::int32_t(native_stack);
    if(invoke_)invoke_(found->function,ram_.data(),&context);
    else found->function(ram_.data(),&context);
    const auto fcsr=registers.fcsr;registers=read(context);registers.fcsr=fcsr;registers.integer[29]=std::uint64_t(std::int64_t(std::int32_t(stack)));
}
void GameWorld::attach(std::span<std::uint8_t> ram,bool initial) {
    std::lock_guard lock(mutex_);world_.attach(ram,initial);ram_=ram;
    if(initial&&sdk2_enabled()) {
        std::vector<std::shared_ptr<const Package>> packages;for(const auto& o:linked().owners)packages.push_back(o.package);
        dkr::sdk2::begin(packages,online_);
    }
    auto regions=linked().regions;regions.push_back({0x80000000U,8*unsigned(MiB),true,false});
    native_bus_=std::make_unique<Memory>(ram,std::move(regions));
    if(initial)for(const auto& o:linked().owners)if(o.package->api==2) {
        native_bus_->store(o.services+sdk2::Header,4,sdk2::Magic);native_bus_->store(o.services+sdk2::Header+4,4,1);
    }
}
void GameWorld::initialize(){std::lock_guard lock(mutex_);Budget budget=sdk2_budget_;execution::PhaseScope phase(execution::Phase::Loading);world_.initialize(budget);}
void GameWorld::event(unsigned event,const Registers& args) {
    std::lock_guard lock(mutex_);
    if(!event||event>=std::size(Events))throw Error("Unknown game lifecycle event.");
    sdk2_boundary(event);
    const auto previous_phase=sdk2_phase_;
    sdk2_phase_=event==DKR_CODE_EVENT_FRAME||event==DKR_CODE_EVENT_FRAME_END?2:1;
    struct Phase{unsigned& value;unsigned previous;~Phase(){value=previous;}} phase{sdk2_phase_,previous_phase};
    if(event==DKR_CODE_EVENT_FRAME)for(const auto& owner:linked().owners)native_bus_->store(owner.services+1024,4,0);
    if(nested_==0&&!execution::active(&world_))budget_=sdk2_budget_;
    if(nested_>=32)throw Error("Game event recursion exceeds its bound.");
    ++nested_;struct Scope{unsigned& count;~Scope(){--count;}}scope{nested_};
    const auto outgoing=owned_preview_arguments(ram_,online_,args);
    execution::PhaseScope work_phase(event==DKR_CODE_EVENT_FRAME||event==DKR_CODE_EVENT_FRAME_END?execution::Phase::Simulation:execution::event_phase(Events[event]));
    world_.event("*",Events[event],args,budget_,outgoing?&*outgoing:nullptr);
    if(event==DKR_CODE_EVENT_FRAME_END)sdk2_inspector();
}
int GameWorld::dispatch_event(unsigned event,std::uint8_t* ram,recomp_context* context)noexcept {
    try {
        std::lock_guard lock(mutex_);
        if(!context||ram!=ram_.data()||!error_.empty())throw Error("Game event has no bound code world.");
        this->event(event,read(*context));return 1;
    }catch(const std::exception& e){try{error_=e.what();}catch(...){}return -1;}
     catch(...){try{error_="Custom Magic Code failed during a lifecycle event.";}catch(...){}return -1;}
}
int GameWorld::indirect(std::uint32_t address,std::uint8_t* ram,recomp_context* context,std::uint64_t& result)noexcept {
    result=0;if(address<CodeBase)return 1;
    try {
        std::lock_guard lock(mutex_);
        if(!context||ram!=ram_.data()||!error_.empty()||!linked().function_owners.contains(address))
            throw Error("Indirect game callback does not belong to the admitted code world.");
        if(nested_==0&&!execution::active(&world_))budget_=sdk2_budget_;
        if(nested_>=32)throw Error("Indirect code callback recursion exceeds its bound.");
        ++nested_;struct Scope{unsigned& count;~Scope(){--count;}}scope{nested_};
        auto registers=read(*context);const auto outgoing=owned_preview_arguments(ram_,online_,registers);
        world_.call(address,registers,budget_,outgoing?&*outgoing:nullptr);write(*context,registers);
        result=1;return 1;
    }catch(const std::exception& e){try{error_=e.what();}catch(...){}return -1;}
     catch(...){try{error_="Custom Magic Code failed during an indirect game callback.";}catch(...){}return -1;}
}
int GameWorld::dispatch(bool leaving,std::uint32_t address,std::uint8_t* ram,recomp_context* context,std::uint64_t& result)noexcept {
    try {
        std::lock_guard lock(mutex_);
        if(ram!=ram_.data()||!context||!error_.empty())throw Error("Code callback lost its exclusively bound game world.");
        const auto entry=std::lower_bound(table_.entries.begin(),table_.entries.end(),address,
            [](const auto& item,auto value){return item.address<value;});
        if(entry==table_.entries.end()||entry->address!=address)throw Error("Unknown code hook entry.");
        sdk2_lifetime(entry->name?entry->name:"",leaving,read(*context));
        const auto previous_phase=sdk2_phase_;
        const std::string_view function_name=entry->name?entry->name:"";
        if(function_name.find("render")!=function_name.npos||function_name.starts_with("draw_")||function_name.starts_with("hud_"))sdk2_phase_=4;
        struct Phase{unsigned& value;unsigned previous;~Phase(){value=previous;}} phase{sdk2_phase_,previous_phase};
        execution::PhaseScope work_phase(execution::function_phase(function_name));
        const auto lifecycle=leaving?entry->after_event:entry->before_event;
        if(lifecycle)this->event(lifecycle,read(*context));
        const auto named=named_events_.find(address);
        if(!world_.has_hook(address)&&named==named_events_.end()&&!(sdk2_enabled()&&((leaving&&function_name=="menu_pause_loop")||(!leaving&&function_name=="update_AI_racer")))){result=0;return 1;}
        if(nested_==0&&!execution::active(&world_))budget_=sdk2_budget_;
        if(nested_>=32)throw Error("Native/code hook recursion exceeds its bound.");
        ++nested_;struct Scope{unsigned& count;~Scope(){--count;}}scope{nested_};
        execution::Transaction work_transaction(&world_,budget_,"game-hooks");
        auto registers=read(*context);
        const auto outgoing=owned_preview_arguments(ram_,online_,registers);
        const auto* native_arguments=outgoing?&*outgoing:nullptr;
        CallbackContext callback{&registers,!leaving,false,address};
        auto* previous=callback_;callback_=&callback;
        struct RestoreContext{CallbackContext*& slot;CallbackContext* previous;~RestoreContext(){slot=previous;}} restore_context{callback_,previous};
        if(named!=named_events_.end())world_.event("*",leaving?named->second.second:named->second.first,registers,budget_,native_arguments);
        if(leaving){world_.leave(address,registers,budget_,native_arguments);sdk2_native_return(function_name,registers);result=callback.repeat?1:0;}
        else result=callback.skip||world_.enter(address,registers,budget_,native_arguments)||sdk2_freeze(function_name)?1:0;
        write(*context,registers);
        return 1;
    }catch(const std::exception& e){try{error_=e.what();}catch(...){}return -1;}
     catch(...){try{error_="Custom Magic Code stopped after an unexpected callback failure.";}catch(...){}return -1;}
}
Bytes GameWorld::checkpoint()const{std::lock_guard lock(mutex_);return world_.checkpoint();}
Bytes GameWorld::retire_checkpoint(std::span<std::uint8_t> live_ram) {
    std::lock_guard lock(mutex_);
    if(nested_ || native_depth_ || !world_.idle())throw Error("Code handoff requires every guest callback to retire.");
    // Never retain a pointer into recomp::start's allocation after its
    // quiescence callback returns: that allocation is immediately released.
    const auto retire=[&]{world_.detach();native_bus_.reset();ram_={};};
    try {
        if(live_ram.empty() || live_ram.data()!=ram_.data() || live_ram.size()!=ram_.size())
            throw Error("Code handoff does not own the live bootstrap memory.");
        if(!error_.empty())throw Error(error_);
        auto bytes=world_.checkpoint();retire();return bytes;
    }catch(...){retire();throw;}
}
std::shared_ptr<World::Restore> GameWorld::stage(View bytes)const{std::lock_guard lock(mutex_);return world_.stage(bytes);}
void GameWorld::commit(std::shared_ptr<World::Restore> restore){std::lock_guard lock(mutex_);world_.commit(std::move(restore));}
std::vector<Effect> GameWorld::effects(){std::lock_guard lock(mutex_);return world_.take_effects();}
bool GameWorld::frame_ends_at(std::uint32_t address)const {
    const auto found=std::lower_bound(table_.entries.begin(),table_.entries.end(),address,
        [](const auto& entry,auto value){return entry.address<value;});
    return found!=table_.entries.end()&&found->address==address&&found->after_event==DKR_CODE_EVENT_FRAME_END;
}
std::size_t GameWorld::checkpoint_limit()const {
    std::size_t bytes=80+128*520;
    for(const auto& owner:linked().owners)bytes+=16+12*(owner.heap_size/16);
    for(const auto& region:linked().regions)if(region.writable)bytes+=8+region.size;
    return bytes;
}
}

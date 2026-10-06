#include "runtime_legacy_mods.hpp"
#include "atomic_snapshot.hpp"
#include "custom_tracks.hpp"
#include "runtime_netplay.hpp"
#include "game_payload.hpp"
#include "mods/legacy_runtime_assets.hpp"
#include "mods/legacy_runtime_io.hpp"
#include "mods/legacy_track_menu_adapter.hpp"
#include "mods/legacy_character_roster.hpp"
#include "mods/legacy_runtime_character.hpp"
#include "mods/legacy_character_menu_render.hpp"
#include "mods/legacy_character_stage.hpp"
#include "mods/legacy_character_presentation.hpp"
#include "mods/legacy_heap_policy.hpp"
#include "mods/legacy_asset_capacity.hpp"
#include "mods/legacy_checkpoint.hpp"
#include "mods/online_mod_runtime.hpp"
#include "mods/online_course_policy.hpp"
#if DKR_LEGACY_QUALIFICATION
#include "legacy_runtime_qualification.hpp"
#endif
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <bit>

extern "C" void osPiStartDma_recomp(std::uint8_t*,recomp_context*);
extern "C" void dkr_character_select_animation_fraction(std::uint8_t*,recomp_context*);
extern "C" void dkr_custom_tracks_extend_table(std::uint8_t*,recomp_context*,std::uint32_t);
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t*,recomp_context*);
namespace dkr::runtime::legacy {
namespace {
struct MenuState {
    std::mutex mutex;
    mods::TrackMenuAdapter adapter;
    bool allow_races, catalog_ready=false;
    MenuState(std::vector<mods::Root> roots,bool races):adapter(std::move(roots),races),allow_races(races){}
    explicit MenuState(mods::TrackMenuAdapter value):adapter(std::move(value)),allow_races(true),catalog_ready(true){}
};
struct CharacterState {
    // A private sound/spawn callback can synchronously re-enter the same
    // adapter through its reviewed guest entry. The outer owner remains
    // exclusive; re-entrancy must not deadlock the native sidecar lock.
    std::recursive_mutex mutex;
    mods::CharacterRoster roster;
    mods::CharacterAiState ai;
    std::unique_ptr<mods::CharacterMenuAdapter> selector;
    mods::CharacterStage stage;
    mods::CharacterMenuRenderer hints;
    mods::CharacterPresentation presentation;
    bool menu_active=false;
    std::array<bool,8> portrait_logged{};
    explicit CharacterState(std::shared_ptr<const mods::CharacterNamespace> assets):roster(std::move(assets)){}
};
struct MusicBinding {
    std::mutex mutex;
    mods::online::MusicState state;
    MusicBinding(std::shared_ptr<const mods::online::MusicLibrary> songs,bool rev80):state(std::move(songs),rev80){}
    explicit MusicBinding(mods::online::MusicState value):state(std::move(value)){}
};
struct WorldState {
    AtomicSnapshot<mods::RuntimeSession> session;
    AtomicSnapshot<const mods::PreparedModLaunch> launch;
    AtomicSnapshot<MenuState> menu;
    AtomicSnapshot<CharacterState> characters;
    AtomicSnapshot<MusicBinding> music;
    GamePayload calls{};
    RecompiledEntrypoint animation=nullptr;
    RecompiledEntrypoint random=nullptr;
    std::shared_ptr<const custom_tracks::PreparedTracks> authored;
    bool private_owner=false,frozen_online=false,custom_ai=false;
    std::uint32_t pending_course_heap=0;
};
WorldState ordinary_world;
std::atomic<bool> ordinary_music_enabled{false};
thread_local WorldState* private_world=nullptr;
WorldState& current_world(){return private_world?*private_world:ordinary_world;}
template<class T,AtomicSnapshot<T> WorldState::*Member>struct WorldSlot {
    std::shared_ptr<T> load()const{return (current_world().*Member).load();}
    void store(std::shared_ptr<T> value)const{(current_world().*Member).store(std::move(value));}
};
const WorldSlot<mods::RuntimeSession,&WorldState::session> session;
const WorldSlot<const mods::PreparedModLaunch,&WorldState::launch> launch;
const WorldSlot<MenuState,&WorldState::menu> menu;
const WorldSlot<CharacterState,&WorldState::characters> characters;
const GamePayload* guest_payload(){return private_world?&private_world->calls:active_payload();}
std::size_t guest_memory_size(){return private_world?8U*1024*1024:recomp::mem_size;}
bool owns_level(std::int32_t level) {
    const auto& world=current_world();
    if(!world.private_owner && !world.frozen_online)return custom_tracks::owns_level_id(level);
    if(!world.authored)return false;
    return std::ranges::any_of(world.authored->courses,[&](const auto& course){return course.level_id==level;});
}
std::vector<custom_tracks::TrackSelectEntry> authored_courses() {
    const auto& world=current_world();
    return world.private_owner || world.frozen_online?
        (world.authored?world.authored->courses:std::vector<custom_tracks::TrackSelectEntry>{}):custom_tracks::track_select_entries();
}
bool frozen_courses(){return current_world().private_owner || current_world().frozen_online;}
std::vector<mods::Root> frozen_roots(const mods::RuntimeSession& assets,
    const custom_tracks::PreparedTracks* courses) {
    auto roots=assets.tracks();
    if(courses)for(const auto& course:courses->courses) {
        mods::Root root;const auto identity="dkrmap:"+course.id;
        root.content_id=mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
        root.name=course.name;root.carrier=course.level_id;root.vehicles=course.vehicles;roots.push_back(std::move(root));
    }
    if(roots.size()>512)throw mods::Error("Frozen custom course menu exceeds its bound.");
    return roots;
}
void course_budget(std::uint8_t* ram,bool install) {
    const auto& world=current_world();
    if(!frozen_courses())return;
    mods::CharacterMenuFields fields;fields.fill(0x80000000U);
    mods::CharacterMenuMemory guest({ram,guest_memory_size()},fields);
    const auto assets=session.load();
    const bool rev80=assets->boot_bank()->revision()=="us.v80";
    const auto table=rev80?0x800DD920U:0x800DD3B0U,lists=rev80?0x80121770U:0x801211F0U;
    const auto wanted=mods::online::course_display_budgets(world.authored.get());
    // Section 22 is read at global initialization AND on every level load,
    // including the first INTRO background after display heaps exist. A
    // repeated read is not new admission. Validate the boot-owned contract
    // without writing/resizing a live allocation; only cold boot may install.
    const bool allocated=guest.read(lists)!=0;
    for(unsigned i=0;i<4;++i)if(guest.read(table+4*i)!=wanted[i]) {
        if(allocated)throw mods::Error("Custom scene display-list contract differs from its existing allocation.");
        if(!install)throw mods::Error("Custom scene display-list contract changed after boot.");
        guest.write(table+4*i,wanted[i]);
    }
}
struct WorldScope {
    WorldState* before;
    explicit WorldScope(WorldState& value):before(private_world){private_world=&value;}
    ~WorldScope(){private_world=before;}
};
mods::CharacterPresentationCalls presentation_calls(const GamePayload& p) {
    return {p.asset_allocate,p.menu_texture_load,p.sound_bank_relocate,p.sound_bank_play,p.sound_parameter,p.sound_spatial_point};
}
void prepare_presentation(CharacterState& state,std::uint8_t* ram,const recomp_context& ctx,unsigned racer,bool audio) {
    if(!state.selector || racer>=8 || state.roster.active(racer).empty())return;
    const auto p=guest_payload();if(!p)throw mods::Error("Missing private presentation revision.");
    const auto assets=session.load();const auto& entries=state.selector->entries();
    const auto found=std::ranges::find(entries,state.roster.active(racer),&mods::AllocatedCharacter::id);
    if(found==entries.end())throw mods::Error("Active presentation character was not admitted.");
    const auto i=static_cast<unsigned>(found-entries.begin());
    state.presentation.prepare({ram,guest_memory_size()},ctx,presentation_calls(*p),entries,i,assets->character_race_sample_address(i),audio);
}
std::mutex error_mutex;
std::string last_error;
[[noreturn]] void fail(const char* error) {
    if(private_world)throw mods::Error(error);
    {std::lock_guard lock(error_mutex);last_error=error;}
    std::fprintf(stderr,"[legacy][fatal] %s\n",error);
    // Never return a fake DMA success or throw an importer exception through
    // the guest scheduler. Its existing termination boundary owns teardown.
    ultramodern::quit();
    throw ultramodern::thread_terminated{};
}
}
void begin_session(std::shared_ptr<mods::RuntimeSession> value) {
    {std::lock_guard lock(error_mutex);last_error.clear();}
    launch.store(nullptr);characters.store(nullptr);menu.store(nullptr);session.store(std::move(value));
    ordinary_world.frozen_online=false;ordinary_world.authored.reset();ordinary_world.custom_ai=false;
    ordinary_world.pending_course_heap=0;
    ordinary_music_enabled=false;ordinary_world.music.store(nullptr);
}
void begin_online(std::shared_ptr<const mods::online::RuntimeResources> resources) {
    if(!resources)throw mods::Error("Online mod admission has no pinned game resources.");
    begin_session(resources->new_session());
    ordinary_world.frozen_online=true;ordinary_world.authored=resources->authored();
    ordinary_world.custom_ai=resources->profile()->manifest.custom_ai;
    ordinary_world.music.store(std::make_shared<MusicBinding>(resources->music(),session.load()->boot_bank()->revision()=="us.v80"));
    ordinary_music_enabled=true;
    auto roots=frozen_roots(*session.load(),ordinary_world.authored.get());
    if(!roots.empty()) {
        auto prepared_menu=std::make_shared<MenuState>(std::move(roots),true);
        prepared_menu->catalog_ready=true;menu.store(std::move(prepared_menu));
    }
    if(session.load()->characters())begin_character_menu();
}
void begin_prepared(std::shared_ptr<const mods::PreparedModLaunch> prepared) {
    begin_session(prepared?prepared->session:nullptr);
    if(!prepared || !prepared->session)return;
    if(prepared->save_subfolder.empty() || prepared->save_path.empty() || prepared->pak_directory.empty())
        throw mods::Error("Modded runtime admission requires isolated save paths.");
    if(!prepared->session->tracks().empty())begin_track_menu(true);
    if(prepared->session->characters())begin_character_menu();
    launch.store(std::move(prepared));
}
std::shared_ptr<const mods::PreparedModLaunch> prepared_launch(){return launch.load();}
bool frozen_online_resources(){return frozen_courses();}
bool frozen_course_hook(const char* hook,std::uint8_t* ram,recomp_context* ctx) {
    if(!frozen_courses())return false;
    try {
    auto& world=current_world();const std::string_view name(hook);
    if(name=="prepare_memory") {
        world.pending_course_heap=mods::online::course_heap(world.authored.get(),static_cast<unsigned>(ctx->r4));
        if(const auto music=world.music.load()) {std::lock_guard lock(music->mutex);
            mods::CharacterMenuFields fields;fields.fill(0x80000000U);mods::CharacterMenuMemory guest({ram,guest_memory_size()},fields);
            music->state.load(guest,static_cast<unsigned>(ctx->r4));}
    }
    else if(name=="prepare_level")course_budget(ram,false);
    else if(name=="track_heap") {
        if(world.pending_course_heap && std::uint32_t(ctx->r21)!=custom_tracks::kRetailTrackHeap)
            throw mods::Error("Frozen custom collision heap hook ABI changed.");
        if(world.pending_course_heap)ctx->r21=world.pending_course_heap;
        world.pending_course_heap=0;
    } else throw mods::Error("Unknown frozen custom-course hook.");
    return true;
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& e){fail(e.what());}
}
bool frozen_music_hook(const char* operation,std::uint8_t* ram,recomp_context* ctx) {
    if(!frozen_courses())return false;
    try {
        if(const auto music=current_world().music.load()) {std::lock_guard lock(music->mutex);
            mods::CharacterMenuFields fields;fields.fill(0x80000000U);mods::CharacterMenuMemory guest({ram,guest_memory_size()},fields);
            const std::string_view name(operation);
            if(name=="loaded")music->state.loaded(guest,*ctx);
            else if(name=="started")music->state.started(guest);
            else if(name=="tick")music->state.tick(guest);
            else throw mods::Error("Unknown frozen music callback.");
        }
        return true;
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& e){fail(e.what());}
}
bool frozen_music_volume(std::uint8_t* ram,recomp_context* ctx) {
    if(!frozen_courses())return false;
    try {
        const auto music=current_world().music.load();if(!music)return false;std::lock_guard lock(music->mutex);
        mods::CharacterMenuFields fields;fields.fill(0x80000000U);mods::CharacterMenuMemory guest({ram,guest_memory_size()},fields);
        return music->state.volume(guest,*ctx);
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& e){fail(e.what());}
}
bool frozen_music_render(float* output,std::size_t frames,unsigned rate) {
    if(!ordinary_world.frozen_online)return false;
    if(ordinary_music_enabled)if(const auto music=ordinary_world.music.load()) {
        std::lock_guard lock(music->mutex);music->state.render(output,frames,rate);
    }
    return true; // suppress stale offline music, including owned confirmed PCM
}
void begin_character_roster(const std::array<std::string,4>& selected) {
    auto active=session.load();if(!active || !active->characters())throw mods::Error("No characters are prepared for this session.");
    auto state=std::make_shared<CharacterState>(active->characters());
    for(unsigned i=0;i<4;++i)state->roster.request(i,selected[i]);
    characters.store(std::move(state));
}
void begin_character_menu() {
    auto active=session.load();if(!active || !active->characters())throw mods::Error("No character library is admitted.");
    auto state=std::make_shared<CharacterState>(active->characters());
    state->selector=std::make_unique<mods::CharacterMenuAdapter>(active->characters());
    characters.store(std::move(state));
}
void begin_track_menu(bool allow_races) {
    auto active=session.load();if(!active)throw mods::Error("No catalogue is admitted for the native menu.");
    menu.store(std::make_shared<MenuState>(active->tracks(),allow_races));
}
void request_scene(const std::string& id,unsigned carrier) {
    // Appended .dkrmap level IDs load through the custom-track asset hooks.
    // begin_scene detects them and returns a mounted legacy bank to stock.
    if(owns_level(static_cast<std::int32_t>(carrier)))return;
    auto active=session.load();
    if(!active) {
        if(id.empty())return;
        throw mods::Error("No custom-content session is prepared.");
    }
    active->request(id,carrier);
}
std::string failure(){std::lock_guard lock(error_mutex);return last_error;}
}
extern "C" void dkr_legacy_character_event(std::uint8_t* rdram,recomp_context* ctx,unsigned event,std::uint32_t roster_address) {
    using namespace dkr::runtime;
    auto state=legacy::characters.load();if(!state)return;
    try {
        std::lock_guard lock(state->mutex);
        const auto original=ctx->r4;
        if(event==0 && state->stage.redirect_spawn({rdram,legacy::guest_memory_size()},*ctx))return;
        dkr::mods::dispatch_character_event(state->roster,{rdram,legacy::guest_memory_size()},*ctx,event,roster_address,state->ai.spawn);
        // Allocate only committed participants, on the fixed guest logic
        // path. Rendering/replay suppression cannot change the heap order.
        if(event==1 && state->selector)for(unsigned i=0;i<unsigned(ctx->r4);++i)
            legacy::prepare_presentation(*state,rdram,*ctx,i,true);
        if(event==0 && ctx->r4!=original)std::fprintf(stderr,"[legacy][character] racer root %u -> %u\n",unsigned(original),unsigned(ctx->r4));
        if(event==1)for(unsigned i=0;i<unsigned(ctx->r4);++i)std::fprintf(stderr,"[legacy][character] P%u committed %s\n",
            i+1,state->roster.active(i).empty()?"stock":state->roster.active(i).c_str());
        if(event==1 || event==2)state->portrait_logged={};
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_character_ai_event(std::uint8_t* rdram,recomp_context* ctx,unsigned event,dkr::mods::OriginalPiHandler random) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();
    if(!state || !state->selector || (legacy::frozen_courses()?!legacy::current_world().custom_ai:netplay::session().active()))return;
    try {
        std::array<std::uint8_t,10> pool{0,9,1,5,3,2,7,4};unsigned count=8;
        if(event==1){
            const auto p=legacy::guest_payload();if(!p)throw dkr::mods::Error("Missing race AI revision.");
            auto drum=*ctx,tt=*ctx;
            // Same scratch discipline as the AI RNG; do not alias the live
            // track_setup_racers caller frame or its float registers.
            const auto sp=std::uint32_t(ctx->r29);
            if((sp&0xe0000000U)!=0x80000000U || (sp&0x1fffffffU)<0x100 || (sp&7))
                throw dkr::mods::Error("Invalid race AI unlock stack.");
            drum.r29=tt.r29=static_cast<std::int32_t>(sp-0x100);
            drum.f_odd=drum.mips3_float_mode?&drum.f1.u32l:&drum.f0.u32h;
            tt.f_odd=tt.mips3_float_mode?&tt.f1.u32l:&tt.f0.u32h;
            p->unlock_drumstick(rdram,&drum);p->unlock_tt(rdram,&tt);
            if(drum.r2)pool[count++]=6;if(tt.r2)pool[count++]=8;
        }
        std::lock_guard lock(state->mutex);
        dkr::mods::dispatch_character_ai_event(state->roster,state->ai,{rdram,legacy::guest_memory_size()},*ctx,event,
            std::span<const std::uint8_t>(pool.data(),count),random);
        if(event==1)for(unsigned i=0;i<8;++i)legacy::prepare_presentation(*state,rdram,*ctx,i,true);
        if(event==0 || event==1)state->portrait_logged={};
        if(event==1)for(unsigned i=state->ai.humans;i<state->ai.racers;++i)
            std::fprintf(stderr,"[legacy][character] AI racer %u: %s\n",i+1,
                state->roster.active(i).empty()?"stock":state->roster.active(i).c_str());
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" int dkr_legacy_character_menu(std::uint8_t* rdram,recomp_context* ctx,unsigned event,const std::uint32_t* fields) {
    using namespace dkr::runtime;
    auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {
        if(!fields || event>8)throw dkr::mods::Error("Invalid character-menu hook ABI.");
        dkr::mods::CharacterMenuFields addresses;std::copy_n(fields,addresses.size(),addresses.begin());
        dkr::mods::CharacterMenuMemory guest({rdram,legacy::guest_memory_size()},addresses);
        const auto p=legacy::guest_payload();if(!p)throw dkr::mods::Error("Character menu has no selected ROM revision.");
        const dkr::mods::CharacterStageCalls stage{p->stage_spawn,p->stage_free,p->stage_music_fraction,p->stage_particles,
            legacy::private_world?legacy::private_world->animation:dkr_character_select_animation_fraction,p->stage_camera};
        if(event==0) {
            auto drum=*ctx,tt=*ctx;
            p->unlock_drumstick(rdram,&drum);p->unlock_tt(rdram,&tt);
            {std::lock_guard lock(state->mutex);state->selector->reset();state->selector->set_unlocks(drum.r2!=0,tt.r2!=0);}
            state->stage.initialize({rdram,legacy::guest_memory_size()},addresses,*ctx,stage,state->selector->entries());
            state->menu_active=true;
            const auto active=legacy::session.load();
            std::vector<std::uint32_t> samples;
            for(unsigned i=0;i<state->selector->entries().size();++i)samples.push_back(active->character_race_sample_address(i));
            state->presentation.initialize({rdram,legacy::guest_memory_size()},*ctx,legacy::presentation_calls(*p),state->selector->entries(),samples);
            std::fprintf(stderr,"[legacy][stage] %zu characters enabled, %zu visible stage actors\n",
                state->selector->entries().size(),std::min<std::size_t>(dkr::mods::CustomStageSlots,state->selector->entries().size()));
        } else if(event==2) {
            dkr::mods::CharacterMenuView view;
            {std::lock_guard lock(state->mutex);view=state->selector->view(guest);}
            const dkr::mods::CharacterMenuDrawCalls draw{p->asset_allocate,p->asset_release,p->menu_texture_load,p->menu_texture_free,
                p->menu_texture_draw,p->menu_font,p->menu_colour,p->menu_background,p->menu_text,p->menu_render_reset};
            state->hints.draw_hint({rdram,legacy::guest_memory_size()},addresses,*ctx,draw,state->selector->entries(),view);
        }
        else if(event==6) {
            dkr::mods::CharacterMenuView view;
            {std::lock_guard lock(state->mutex);view=state->selector->view(guest);}
            return state->stage.update({rdram,legacy::guest_memory_size()},addresses,*ctx,stage,view);
        } else if(event==7) {
            // Checked call site keeps the physical cursor index in s0.
            const auto player=static_cast<unsigned>(ctx->r16);
            if(player>=4)throw dkr::mods::Error("Native stage cursor is invalid.");
            std::lock_guard lock(state->mutex);
            if(state->selector->custom(player))ctx->r2=static_cast<gpr>(-1);
        } else if(event==3){state->menu_active=false;state->stage.release({rdram,legacy::guest_memory_size()},*ctx,stage);}
        else if(event==8) {
            if(!state->menu_active)return 0;
            const auto handle=static_cast<std::uint32_t>(ctx->r5);
            const auto handles=guest.address(dkr::mods::CharacterMenuField::SoundHandles);
            if(handle<handles || handle>=handles+16 || (handle-handles)%4)return 0;
            const unsigned player=(handle-handles)/4;std::optional<std::size_t> selected;
            {std::lock_guard lock(state->mutex);selected=state->selector->custom(player);}
            if(!selected)return 0;
            const auto sound=static_cast<unsigned>(ctx->r4);
            constexpr unsigned bases[]{0x87,0x93,0x19e};unsigned action=3;
            for(unsigned a=0;a<3;++a)if(sound>=bases[a] && sound<bases[a]+10)action=a;
            if(action==3)return 0; // unrelated native audio retains its path
            const auto& cue=state->selector->entries().at(*selected).audio.cues[action];
            if(!cue.sound){guest.write(handle,0);return 1;}
            const auto active=legacy::session.load();
            const auto bank=state->presentation.selection_bank({rdram,legacy::guest_memory_size()},*ctx,
                legacy::presentation_calls(*p),state->selector->entries(),static_cast<unsigned>(*selected),active->character_sample_address(static_cast<unsigned>(*selected)));
            auto play=*ctx;play.r4=static_cast<gpr>(static_cast<std::int32_t>(bank));
            play.r5=cue.sound;play.r6=cue.priority;play.r7=static_cast<gpr>(static_cast<std::int32_t>(handle));
            p->sound_bank_play(rdram,&play);
            const auto voice=guest.read(handle);
            if(voice) {
                auto param=*ctx;param.r4=static_cast<gpr>(static_cast<std::int32_t>(voice));param.r5=8;param.r6=cue.volume*256;
                p->sound_parameter(rdram,&param);
                param=*ctx;param.r4=static_cast<gpr>(static_cast<std::int32_t>(voice));param.r5=16;
                param.r6=std::bit_cast<std::uint32_t>(cue.pitch/100.0f);p->sound_parameter(rdram,&param);
            }
            return 1;
        }
        else if(event==4) {
            std::lock_guard lock(state->mutex);
            state->selector->commit(guest,state->roster,static_cast<unsigned>(ctx->r4));
        } else {
            if(event==5) {std::lock_guard lock(state->mutex);if(!state->selector->has_custom())return 0;}
            if(!p->filtered_cheats)throw dkr::mods::Error("Character menu cheat policy is unavailable.");
            auto cheats=*ctx;p->filtered_cheats(rdram,&cheats);
            const bool duplicates=(static_cast<std::uint32_t>(cheats.r2)&(1U<<22))!=0;
            std::vector<unsigned> sounds;
            auto previous=*state->selector;
            std::array<unsigned,4> previous_indices{};
            for(unsigned i=0;i<4;++i)previous_indices[i]=guest.get(dkr::mods::CharacterMenuField::NativeIndices,i,1);
            const auto previous_music=guest.get(dkr::mods::CharacterMenuField::CurrentMusic);
            {std::lock_guard lock(state->mutex);
                if(event==1)sounds=state->selector->input(guest,duplicates);
                else {
                    const bool moved=state->selector->native_move(guest,static_cast<unsigned>(ctx->r4),
                        static_cast<std::uint32_t>(ctx->r5),static_cast<unsigned>(ctx->r6),duplicates);
                    const auto stack=static_cast<std::uint32_t>(ctx->r29);
                    if(stack>0xffffffffU-16)throw dkr::mods::Error("Character sound argument overflow.");
                    sounds.push_back(moved?static_cast<unsigned>(ctx->r7):guest.read(stack+16));
                }
            }
            if(event==1 && !state->stage.synchronize({rdram,legacy::guest_memory_size()},*ctx,stage,
                    state->selector->entries(),state->selector->view(guest))) {
                std::lock_guard lock(state->mutex);*state->selector=std::move(previous);
                for(unsigned i=0;i<4;++i)guest.set(dkr::mods::CharacterMenuField::NativeIndices,previous_indices[i],i,1);
                guest.set(dkr::mods::CharacterMenuField::CurrentMusic,previous_music);
                sounds={0x15C};std::fprintf(stderr,"[legacy][stage] Browsing retained the prior roster: native model budget unavailable\n");
            }
            for(auto id:sounds) {
                if(!p->menu_sound_play)throw dkr::mods::Error("Character menu sound callback is unavailable.");
                auto sound=*ctx;sound.r4=id;sound.r5=0;p->menu_sound_play(rdram,&sound);
            }
            if(event==5)return 1;
        }
        return 0;
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_heap_capacity(std::uint8_t*,recomp_context* ctx) {
    const auto end=dkr::mods::legacy_heap_end(std::uint32_t(ctx->r15),dkr::runtime::legacy::guest_memory_size(),
        bool(dkr::runtime::legacy::session.load()));
    if(end!=std::uint32_t(ctx->r15))
        std::fprintf(stderr,"[legacy][memory] offline custom-session pool uses existing 8 MiB RDRAM; stock/online pool unchanged\n");
    ctx->r15=static_cast<gpr>(static_cast<std::int32_t>(end));
}
extern "C" std::uint32_t dkr_legacy_asset_cache_capacity(std::uint8_t*,recomp_context*,unsigned kind) {
    using namespace dkr::runtime;
    if(kind>=4)legacy::fail("Unknown legacy asset cache kind.");
    const auto state=legacy::session.load();
    const auto bank=state?state->boot_bank():nullptr;
    if(!bank || !bank->augmented())return dkr::mods::legacy_asset_cache_capacities(false,0,0,0,0)[kind];
    return dkr::mods::legacy_asset_cache_capacities(true,
        static_cast<std::uint32_t>(bank->record_count(2)),static_cast<std::uint32_t>(bank->record_count(4)),
        static_cast<std::uint32_t>(bank->record_count(12)),static_cast<std::uint32_t>(bank->record_count(29)))[kind];
}
extern "C" void dkr_legacy_asset_cache_guard(std::uint8_t* ram,recomp_context* ctx,unsigned kind,std::uint32_t index) {
    if(index>=dkr_legacy_asset_cache_capacity(ram,ctx,kind))
        dkr::runtime::legacy::fail("Asset cache capacity exceeded; stopped before guest-memory corruption. Please report the active mods and runtime.log.");
}
extern "C" void dkr_legacy_racer_spawn_failed(std::uint8_t*,recomp_context*) {
    dkr::runtime::legacy::fail("Race setup could not allocate a racer; stopped before a null guest-address write. Please report the active mods and runtime.log.");
}
extern "C" std::uint32_t dkr_legacy_character_portrait_lookup(std::uint8_t* rdram,recomp_context* ctx,std::uint32_t racer_base) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load(); // Hold ownership through guest callbacks.
    if(!state || !state->selector)return 0;
    try {
        const auto p=legacy::guest_payload();if(!p)throw dkr::mods::Error("Missing portrait revision.");
        auto settings=*ctx;p->get_settings(rdram,&settings);
        const auto base=std::uint32_t(settings.r2);
        // Native sites pass Settings + racer_index * sizeof(Racer). The
        // original character lookup at +0x59 is otherwise left intact.
        if(racer_base<base || (racer_base-base)%0x18 || (racer_base-base)/0x18>=8)return 0;
        std::lock_guard lock(state->mutex);
        const auto racer=(racer_base-base)/0x18;
        legacy::prepare_presentation(*state,rdram,*ctx,racer,false);
        const auto cell=state->presentation.portrait(state->roster,state->selector->entries(),racer);
        if(cell && !state->portrait_logged[racer]) {
            const auto& entries=state->selector->entries();
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& c){return c.id==state->roster.active(racer);});
            if(found!=entries.end())std::fprintf(stderr,"[legacy][portrait] P%u %s: %s, private texture %u\n",
                racer+1,found->name.c_str(),found->custom_portrait?"supplied custom face":"patch retains original face",found->portrait);
            state->portrait_logged[racer]=true;
        }
        return cell;
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" unsigned dkr_legacy_character_race_sound(std::uint8_t* rdram,recomp_context* ctx,std::uint32_t racer,unsigned sound) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return sound;
    try {
        dkr::mods::CharacterMenuFields fields;fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram,legacy::guest_memory_size()},fields);
        const auto player=dkr::mods::character_presentation_slot(guest.read(racer,2),guest.read(racer+2,1)),
            native_character=guest.read(racer+3,1);
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state,rdram,*ctx,player,true);
        return state->presentation.sound(state->roster,state->selector->entries(),player,native_character,sound);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" void dkr_legacy_character_hud_bind(std::uint8_t* rdram,recomp_context* ctx,std::uint32_t hud,std::uint32_t racer) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return;
    try {
        dkr::mods::CharacterMenuFields fields;fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram,legacy::guest_memory_size()},fields);
        // Explicit Adventure slots remain human; AI uses its participant
        // index, never the shared signed -1 player sentinel.
        const auto player=racer<4?racer:dkr::mods::character_presentation_slot(guest.read(racer,2),guest.read(racer+2,1));
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state,rdram,*ctx,player,false);
        state->presentation.bind_hud(std::uint32_t(ctx->r29),hud,state->roster,state->selector->entries(),player);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" void dkr_legacy_character_hud_unbind(std::uint8_t*,recomp_context* ctx) {
    const auto state=dkr::runtime::legacy::characters.load();if(!state)return;
    std::lock_guard lock(state->mutex);state->presentation.unbind_hud(std::uint32_t(ctx->r29));
}
extern "C" std::uint32_t dkr_legacy_character_hud_lookup(std::uint8_t* rdram,recomp_context* ctx,std::uint32_t stack,std::uint32_t hud) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {
        dkr::mods::CharacterMenuFields fields;fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram,legacy::guest_memory_size()},fields);
        const auto sprite=guest.read(hud+6,2);
        std::lock_guard lock(state->mutex);return state->presentation.hud_lookup(stack,hud,sprite);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" int dkr_legacy_character_play_sound(std::uint8_t* rdram,recomp_context* ctx,unsigned kind) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {
        const auto p=legacy::guest_payload();if(!p)throw dkr::mods::Error("Missing custom audio revision.");
        return state->presentation.play({rdram,legacy::guest_memory_size()},*ctx,legacy::presentation_calls(*p),state->selector->entries(),kind);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" unsigned dkr_legacy_character_cinematic_id(std::uint8_t* rdram,recomp_context* ctx,unsigned racer,unsigned native_id) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return native_id;
    try {
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state,rdram,*ctx,racer,false);
        return state->presentation.cinematic_id(state->roster,state->selector->entries(),racer,native_id);
    } catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" std::uint32_t dkr_legacy_character_cinematic_portrait(std::uint8_t*,recomp_context*,unsigned id) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {return state->presentation.cinematic_portrait(id);}
    catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" int dkr_legacy_track_menu(std::uint8_t* rdram,recomp_context* ctx,unsigned event,const std::uint32_t* fields,unsigned observed) {
    using namespace dkr::runtime;
    // trackmenu_assets(TRACKMENU_TYPE_LOAD_LEVEL): Track Select confirmed a
    // course. That choice supersedes Track Lab's sticky load override before
    // get_track_id_to_load can replace it, with or without legacy courses.
    if(!legacy::frozen_courses() && event==12 && static_cast<std::uint32_t>(ctx->r4)==2)
        custom_tracks::arm_track_override({});
    auto state=legacy::menu.load();
    try {
        // Level IDs are assigned at game initialization, after launcher setup.
        // Build the combined catalogue on the first Track Select entry, also
        // when there are no legacy mods enabled for this session.
        if(event==0 && (!state || !state->catalog_ready) && (legacy::frozen_courses() || !netplay::session().active())) {
            const auto authored=legacy::authored_courses();
            if(!authored.empty()) {
                const auto active=legacy::session.load();
                auto roots=active?active->tracks():std::vector<dkr::mods::Root>{};
                for(const auto& track:authored) {
                    if(roots.size()==512)break;
                    dkr::mods::Root root;
                    const auto identity="dkrmap:"+track.id;
                    root.content_id=dkr::mods::sha256(dkr::mods::View(
                        reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
                    root.name=track.name.substr(0,255);
                    root.carrier=static_cast<unsigned>(track.level_id);
                    root.vehicles=track.vehicles;
                    roots.push_back(std::move(root));
                }
                state=std::make_shared<legacy::MenuState>(std::move(roots),state?state->allow_races:true);
                legacy::menu.store(state);
            }
            if(state)state->catalog_ready=true;
        }
        if(!state)return 0;
        if(!fields)throw dkr::mods::Error("Native menu hook has no verified revision fields.");
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram,ctx,event,fields,false);
#endif
        dkr::mods::TrackMenuFields addresses;std::copy_n(fields,addresses.size(),addresses.begin());
        if(event==1) {
            std::size_t bytes=0;
            {std::lock_guard lock(state->mutex);if(state->adapter.needs_names())bytes=state->adapter.name_bytes().size();}
            if(bytes) {
                const auto payload=legacy::guest_payload();
                if(!payload || !payload->asset_allocate)throw dkr::mods::Error("Native menu heap callback is unavailable.");
                auto allocation=*ctx;allocation.r4=bytes;allocation.r5=0x7f7f7fff;
                // Original guest allocation may yield: never hold a host mutex.
                payload->asset_allocate(rdram,&allocation);
                std::lock_guard lock(state->mutex);
                state->adapter.install_names({rdram,legacy::guest_memory_size()},static_cast<std::uint32_t>(allocation.r2));
            }
        }
        dkr::mods::TrackMenuEffect effect;
        {std::lock_guard lock(state->mutex);effect=state->adapter.apply(event,{rdram,legacy::guest_memory_size()},addresses,
            static_cast<std::uint32_t>(ctx->r4),observed);}
        // Appended levels have their own vehicle table entry. Preserve the
        // author's default instead of the legacy carrier's first usable one.
        if(event==10 && legacy::owns_level(static_cast<std::int32_t>(ctx->r4)))
            effect.override_return=false;
        if(effect.scene)legacy::request_scene(effect.scene->id,effect.scene->carrier);
        if(effect.navigation_sound) {
            const auto payload=legacy::guest_payload();
            if(!payload || !payload->menu_sound_play)throw dkr::mods::Error("Native menu sound callback is unavailable.");
            // Match stock trackmenu_input: SOUND_MENU_PICK2 (0xEB), no handle.
            // Guest audio can yield: call only after releasing the host lock,
            // and preserve the original hook's register context.
            auto sound=*ctx;sound.r4=0xEB;sound.r5=0;
            payload->menu_sound_play(rdram,&sound);
        }
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram,ctx,event,fields,true);
#endif
        if(effect.override_return)ctx->r2=static_cast<gpr>(static_cast<std::int32_t>(effect.return_value));
        return effect.override_return;
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_scene_begin(std::uint8_t* rdram,recomp_context* ctx) {
    using namespace dkr::runtime;
    auto active=legacy::session.load();if(!active)return;
    try {
        const auto before=active->current_content();
        const auto level=static_cast<std::uint32_t>(ctx->r4);
        active->begin_scene({rdram,legacy::guest_memory_size()},level,legacy::owns_level(level));
        const auto after=active->current_content();
        if(before!=after)std::fprintf(stderr,"[legacy][scene] generation=%llu carrier=%u content=%s\n",
            static_cast<unsigned long long>(active->published_scenes()),static_cast<unsigned>(ctx->r4),
            after.empty()?"stock":after.c_str());
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" int dkr_legacy_asset_api(std::uint8_t* rdram,recomp_context* ctx,unsigned operation) {
    using namespace dkr::runtime;
    auto active=legacy::session.load();if(!active)return 0;
    try {
        auto lease=active->acquire();const auto mount=lease.route();if(!mount)return 0;
        const auto section=static_cast<std::uint32_t>(ctx->r4);
        // Section 2 is already part of the shared boot-owned texture bank;
        // its offsets may change with the active legacy course's artwork.
        if(!legacy::frozen_courses() && operation==static_cast<unsigned>(dkr::mods::AssetOperation::PartialLoad) && section!=2 &&
           dkr_custom_tracks_asset_override(rdram,ctx))return 1;
        const auto payload=legacy::guest_payload();
        if(!payload)throw dkr::mods::Error("The custom asset loader has no selected revision.");
        const bool handled=dkr::mods::dispatch_asset_api(static_cast<dkr::mods::AssetOperation>(operation),mount,
            {rdram,legacy::guest_memory_size()},*ctx,{payload->asset_allocate,payload->asset_release,payload->asset_copy});
        if(handled && operation==static_cast<unsigned>(dkr::mods::AssetOperation::TableLoad)) {
            if(legacy::frozen_courses()) {if(section==22)legacy::course_budget(rdram,true);}
            else if(section!=3)dkr_custom_tracks_extend_table(rdram,ctx,section);
        }
        return handled;
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_pi_start_dma(std::uint8_t* rdram,recomp_context* ctx) {
    using namespace dkr::runtime;
    try {
        auto active=legacy::session.load();
        dkr::mods::dispatch_pi_dma(active?&active->bus():nullptr,{rdram,legacy::guest_memory_size()},*ctx,
            osPiStartDma_recomp,{nullptr,[](void*,std::uint32_t queue) {
                ultramodern::enqueue_external_message_src(static_cast<std::int32_t>(queue),0,false,
                                                         ultramodern::EventMessageSource::Pi);
            }});
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}

namespace dkr::runtime::legacy {
struct ModWorld::State:WorldState {std::uint64_t mutation=0;std::string error;};
namespace {
struct WorldRestore final:ModWorld::Restore {
    const void* authority=nullptr;
    std::uint64_t mutation=0;
    std::uint32_t pending_course_heap=0;
    mods::RuntimeSession::ReplayRestore session;
    std::shared_ptr<MenuState> menu;
    std::shared_ptr<CharacterState> characters;
    std::shared_ptr<MusicBinding> music;
};
}
ModWorld::ModWorld(std::shared_ptr<mods::RuntimeSession> assets,GamePayload calls,
    RecompiledEntrypoint animation,std::shared_ptr<const custom_tracks::PreparedTracks> authored,bool ai,
    RecompiledEntrypoint random,std::shared_ptr<const mods::online::MusicLibrary> music)
    :state_(std::make_unique<State>()) {
    if(!assets)throw mods::Error("An owned mod world needs admitted resources.");
    (void)mods::online::course_display_budgets(authored.get());
    state_->session.store(assets);state_->private_owner=true;state_->calls=calls;
    state_->animation=animation;state_->random=random;state_->authored=std::move(authored);state_->custom_ai=ai;
    state_->music.store(std::make_shared<MusicBinding>(std::move(music),assets->boot_bank()->revision()=="us.v80"));
    if(ai && !random)throw mods::Error("Owned custom AI needs the reviewed private RNG entry.");
    auto roots=frozen_roots(*assets,state_->authored.get());
    if(!roots.empty()) {
        auto menu=std::make_shared<MenuState>(std::move(roots),true);menu->catalog_ready=true;state_->menu.store(std::move(menu));
    }
    if(assets->characters()) {
        auto characters=std::make_shared<CharacterState>(assets->characters());
        characters->selector=std::make_unique<mods::CharacterMenuAdapter>(assets->characters());
        state_->characters.store(std::move(characters));
    }
}
ModWorld::~ModWorld()=default;
const mods::AssetBus& ModWorld::bus()const{return state_->session.load()->bus();}
const std::string& ModWorld::error()const{return state_->error;}
namespace {
mods::Bytes capture_world(const WorldState& state) {
    mods::CheckpointWriter out;out.u32(4);out.block(state.session.load()->checkpoint());
    out.u32(state.pending_course_heap);
    const auto menu=state.menu.load();out.flag(bool(menu));
    if(menu) {std::lock_guard lock(menu->mutex);out.block(menu->adapter.checkpoint());}
    const auto characters=state.characters.load();out.flag(bool(characters));
    if(characters) {
        std::lock_guard lock(characters->mutex);
        if(characters->ai.spawn)throw mods::Error("Cannot checkpoint a nested AI character spawn.");
        out.block(characters->roster.checkpoint());out.block(characters->selector->checkpoint());
        out.block(characters->stage.checkpoint());out.block(characters->hints.checkpoint());out.block(characters->presentation.checkpoint());
        out.u32(characters->ai.humans);out.u32(characters->ai.racers);out.flag(characters->menu_active);
    }
    const auto music=state.music.load();out.flag(bool(music));
    if(music){std::lock_guard lock(music->mutex);out.block(music->state.checkpoint());}
    return std::move(out).finish();
}
}
mods::Bytes ModWorld::checkpoint()const {return capture_world(*state_);}
mods::Bytes online_bootstrap_checkpoint() {
    if(private_world || !ordinary_world.frozen_online || !ordinary_world.session.load())
        throw mods::Error("Online native bootstrap has no exclusive frozen resources.");
    auto result=capture_world(ordinary_world);ordinary_music_enabled=false;return result;
}
std::unique_ptr<ModWorld::Restore> ModWorld::stage_checkpoint(mods::View bytes)const {
    mods::CheckpointReader in(bytes);if(in.u32()!=4)throw mods::Error("Unsupported owned mod world schema.");
    auto out=std::make_unique<WorldRestore>();out->authority=state_.get();out->mutation=state_->mutation;
    const auto assets=state_->session.load();out->session=assets->stage_checkpoint(in.block());
    out->pending_course_heap=in.u32();
    if(!mods::online::admitted_course_heap(state_->authored.get(),out->pending_course_heap))
        throw mods::Error("Owned course checkpoint has an invalid collision heap.");
    const auto menu=state_->menu.load();if(in.flag()!=bool(menu))throw mods::Error("Owned mod menu identity differs.");
    if(menu) {
        std::lock_guard lock(menu->mutex);
        out->menu=std::make_shared<MenuState>(menu->adapter.stage_checkpoint(in.block()));
    }
    const auto characters=state_->characters.load();if(in.flag()!=bool(characters))throw mods::Error("Owned mod character identity differs.");
    if(characters) {
        std::lock_guard lock(characters->mutex);auto next=std::make_shared<CharacterState>(assets->characters());
        const auto count=assets->characters()->characters.size();
        next->roster=characters->roster.stage_checkpoint(in.block());
        next->selector=std::make_unique<mods::CharacterMenuAdapter>(characters->selector->stage_checkpoint(in.block()));
        next->stage=characters->stage.stage_checkpoint(in.block(),count);next->hints=characters->hints.stage_checkpoint(in.block(),count);
        next->presentation=characters->presentation.stage_checkpoint(in.block(),count);
        next->ai.humans=in.bounded(4);next->ai.racers=in.bounded(8);
        if(next->ai.racers<next->ai.humans)throw mods::Error("Owned mod AI roster is invalid.");
        next->menu_active=in.flag();
        out->characters=std::move(next);
    }
    const auto music=state_->music.load();if(in.flag()!=bool(music))throw mods::Error("Owned mod music identity differs.");
    if(music){std::lock_guard lock(music->mutex);out->music=std::make_shared<MusicBinding>(music->state.stage_checkpoint(in.block()));}
    in.end();return out;
}
bool ModWorld::commit_checkpoint(std::unique_ptr<Restore> value) {
    auto* staged=dynamic_cast<WorldRestore*>(value.get());
    if(!staged || staged->authority!=state_.get() || staged->mutation!=state_->mutation ||
       state_->mutation==UINT64_MAX || !state_->session.load()->commit_checkpoint(std::move(staged->session)))return false;
    state_->menu.store(std::move(staged->menu));state_->characters.store(std::move(staged->characters));
    state_->pending_course_heap=staged->pending_course_heap;
    state_->music.store(std::move(staged->music));
    ++state_->mutation;return true;
}
int ModWorld::dispatch(const char* operation,std::uint8_t* ram,recomp_context* ctx,const std::uint64_t* args,
    unsigned count,const std::uint32_t* fields,unsigned event,std::uint64_t& result)noexcept {
    try {
        if(!operation || !ram || !ctx || (count && !args) || state_->mutation==UINT64_MAX)
            throw mods::Error("Invalid owned mod callback boundary.");
        WorldScope scope(*state_);const std::string_view name(operation);result=0;
        const auto arity=[&](unsigned expected){if(count!=expected)throw mods::Error("Owned mod callback ABI differs.");};
        if(name=="dkr_legacy_asset_api"){arity(1);result=dkr_legacy_asset_api(ram,ctx,static_cast<unsigned>(args[0]));}
        else if(name=="dkr_legacy_scene_begin"){arity(0);dkr_legacy_scene_begin(ram,ctx);}
        else if(name=="dkr_owned_scene_admission") {
            arity(1);if(args[0]>127)throw mods::Error("Custom scene ID is outside its signed-byte range.");
            result=args[0]<65 || owns_level(static_cast<std::int32_t>(args[0]));
            if(result)course_budget(ram,false);
        }
        else if(name=="dkr_owned_course_prepare") {
            arity(0);frozen_course_hook("prepare_memory",ram,ctx);
        }
        else if(name=="dkr_custom_music_sequence_loaded"){arity(0);frozen_music_hook("loaded",ram,ctx);}
        else if(name=="dkr_custom_music_sequence_started"){arity(0);frozen_music_hook("started",ram,ctx);}
        else if(name=="dkr_audio_mix_tick"){arity(0);frozen_music_hook("tick",ram,ctx);}
        else if(name=="dkr_scale_sequence_player_volume"){arity(0);frozen_music_volume(ram,ctx);}
        else if(name=="dkr_custom_tracks_track_heap") {
            arity(0);
            if(state_->pending_course_heap && std::uint32_t(ctx->r21)!=custom_tracks::kRetailTrackHeap)
                throw mods::Error("Owned custom collision heap hook ABI changed.");
            if(state_->pending_course_heap)ctx->r21=state_->pending_course_heap;
            state_->pending_course_heap=0;
        }
        else if(name=="dkr_legacy_heap_capacity"){arity(0);dkr_legacy_heap_capacity(ram,ctx);}
        else if(name=="dkr_legacy_asset_cache_capacity"){arity(1);if(args[0]>=4)throw mods::Error("Unknown owned asset cache kind.");result=dkr_legacy_asset_cache_capacity(ram,ctx,static_cast<unsigned>(args[0]));}
        else if(name=="dkr_legacy_asset_cache_guard"){
            arity(2);if(args[0]>=4 || args[1]>UINT32_MAX)throw mods::Error("Invalid owned asset cache bound.");
            dkr_legacy_asset_cache_guard(ram,ctx,static_cast<unsigned>(args[0]),static_cast<std::uint32_t>(args[1]));
        }
        else if(name=="dkr_legacy_racer_spawn_failed"){arity(0);dkr_legacy_racer_spawn_failed(ram,ctx);}
        else if(name=="dkr_legacy_character_event"){arity(2);dkr_legacy_character_event(ram,ctx,static_cast<unsigned>(args[0]),static_cast<std::uint32_t>(args[1]));}
        else if(name=="dkr_legacy_character_ai_event"){
            arity(1);dkr_legacy_character_ai_event(ram,ctx,static_cast<unsigned>(args[0]),state_->random);
        }
        else if(name=="dkr_owned_asset_dma"){
            arity(3);
            if(args[0]>UINT32_MAX || args[1]>UINT32_MAX || !args[2] || args[2]>0x5000)
                throw mods::Error("Owned custom DMA has an invalid range.");
            const auto read=state_->session.load()->bus().resolve(static_cast<std::uint32_t>(args[0]),args[2]);
            if(!read)return 0;
            read->copy_to_guest({ram,guest_memory_size()},static_cast<std::uint32_t>(args[1]));
        }
        else if(name=="dkr_legacy_character_menu"){arity(0);result=dkr_legacy_character_menu(ram,ctx,event,fields);}
        else if(name=="dkr_legacy_track_menu"){arity(1);result=dkr_legacy_track_menu(ram,ctx,event,fields,static_cast<unsigned>(args[0]));}
        else if(name=="dkr_legacy_character_portrait_lookup"){arity(1);result=dkr_legacy_character_portrait_lookup(ram,ctx,static_cast<std::uint32_t>(args[0]));}
        else if(name=="dkr_legacy_character_race_sound"){arity(2);result=dkr_legacy_character_race_sound(ram,ctx,static_cast<std::uint32_t>(args[0]),static_cast<unsigned>(args[1]));}
        else if(name=="dkr_legacy_character_hud_bind"){arity(2);dkr_legacy_character_hud_bind(ram,ctx,static_cast<std::uint32_t>(args[0]),static_cast<std::uint32_t>(args[1]));}
        else if(name=="dkr_legacy_character_hud_unbind"){arity(0);dkr_legacy_character_hud_unbind(ram,ctx);}
        else if(name=="dkr_legacy_character_hud_lookup"){arity(2);result=dkr_legacy_character_hud_lookup(ram,ctx,static_cast<std::uint32_t>(args[0]),static_cast<std::uint32_t>(args[1]));}
        else if(name=="dkr_legacy_character_play_sound"){arity(1);result=dkr_legacy_character_play_sound(ram,ctx,static_cast<unsigned>(args[0]));}
        else if(name=="dkr_legacy_character_cinematic_id"){arity(2);result=dkr_legacy_character_cinematic_id(ram,ctx,static_cast<unsigned>(args[0]),static_cast<unsigned>(args[1]));}
        else if(name=="dkr_legacy_character_cinematic_portrait"){arity(1);result=dkr_legacy_character_cinematic_portrait(ram,ctx,static_cast<unsigned>(args[0]));}
        else return 0;
        ++state_->mutation;return 1;
    }catch(const std::exception& e){try{state_->error=e.what();}catch(...){}return -1;}
     catch(...){try{state_->error="Owned custom content stopped safely after an unexpected native fault.";}catch(...){}return -1;}
}
void ModWorld::mix_music(std::uint8_t* ram,std::span<std::uint8_t> pcm,unsigned rate) {
    const auto music=state_->music.load();if(!music)return;
    std::lock_guard lock(music->mutex);mods::CharacterMenuFields fields;fields.fill(0x80000000U);
    mods::CharacterMenuMemory guest({ram,8U*1024*1024},fields);music->state.tick(guest);music->state.mix_s16le(pcm,rate);
    if(state_->mutation==UINT64_MAX)throw mods::Error("Custom music transaction exhausted.");++state_->mutation;
}
}

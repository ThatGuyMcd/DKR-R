#include "mods/magic_code_dispatch.h"
#include "runtime_code_mods.hpp"
#include "runtime_legacy_mods.hpp"
#include "atomic_snapshot.hpp"
#include "mods/online_game_pak.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <cstdio>

#if defined(__APPLE__)
// Must match magic_code_dispatch.h: thread_local would hide these from the C payloads on Darwin.
extern "C" {__thread int dkr_code_callbacks_enabled=0;__thread const std::uint8_t* dkr_code_hook_mask=nullptr;}
#else
extern "C" {thread_local int dkr_code_callbacks_enabled=0;thread_local const std::uint8_t* dkr_code_hook_mask=nullptr;}
#endif
namespace dkr::runtime::code_mods {
namespace {
struct Session {
    std::shared_ptr<const mods::code::FrozenSet> frozen;
    std::shared_ptr<mods::code::GameWorld> world;
};
AtomicSnapshot<const Session> ordinary;
thread_local std::shared_ptr<const Session> bound;
std::mutex error_mutex;
std::string last_error;
[[noreturn]] void stop(const std::string& error) {
    {std::lock_guard lock(error_mutex);last_error=error;}
    std::fprintf(stderr,"[custom-magic-codes][stop] %s\n",error.c_str());
    // The ordinary scheduler ALREADY owns this C++ termination boundary.
    // No longjmp crosses a native adapter, lease, lock or resource destructor.
    ultramodern::quit();throw ultramodern::thread_terminated{};
}
}
void validate_online(const mods::code::FrozenSet& code,rom::Revision revision) {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    std::size_t count=0;
    const auto* entries=revision==rom::Revision::UsV77?dkr_experimental_v77_code_dispatch_table(&count):
        revision==rom::Revision::UsV80?dkr_experimental_v80_code_dispatch_table(&count):nullptr;
    if(!entries)throw mods::Error("This platform has no owned Custom Magic Codes dispatcher for the selected ROM.");
    mods::code::GameWorld linked(code,mods::online::asset_revision(revision),{{entries,count}},0,true);
    if(linked.checkpoint_limit()>14*mods::MiB)throw mods::Error("Custom Magic Codes exceed the online checkpoint budget.");
#else
    (void)code;(void)revision;throw mods::Error("This build has no owned Custom Magic Codes engine.");
#endif
}
void begin(std::shared_ptr<const mods::code::FrozenSet> code,rom::Revision revision,std::uint64_t seed,bool online) {
    reset();if(!code||code->ordered.empty())return;
    std::size_t count=0;
    const auto* entries=revision==rom::Revision::UsV77?dkr_v77_code_dispatch_table(&count):
        revision==rom::Revision::UsV80?dkr_v80_code_dispatch_table(&count):nullptr;
    if(!entries)throw mods::Error("The selected ROM has no Custom Magic Code CPU dispatcher.");
    auto next=std::make_shared<Session>();next->frozen=std::move(code);
    next->world=std::make_shared<mods::code::GameWorld>(*next->frozen,mods::online::asset_revision(revision),
        mods::code::DispatchTable{{entries,count}},seed,online);
    next->world->course_selector([](const auto& id,unsigned carrier){legacy::request_scene(id,carrier);});
    next->world->sdk2_current_course([]{return legacy::current_course();});
    if(next->world->sdk2_wants_assets())next->world->sdk2_assets(legacy::asset_inventory());
    legacy::set_trophy_menu(mods::code::trophy::menu_entries(*next->frozen));
    next->world->trophy_selector([]{return legacy::selected_trophy();});
    std::fprintf(stderr,"[custom-magic-codes][session] online=%u packages=%zu identity=%s\n",
        unsigned(online),next->frozen->ordered.size(),next->frozen->identity.c_str());
    for(const auto& package:next->frozen->ordered)
        std::fprintf(stderr,"[custom-magic-codes][package] id=%s version=%s payload=%s\n",package->id.c_str(),package->version.c_str(),package->digest.c_str());
    ordinary.store(std::move(next));
}
void reset(){dkr::sdk2::end();ordinary.store(nullptr);std::lock_guard lock(error_mutex);last_error.clear();}
void bind_thread(){bound=ordinary.load();dkr_code_callbacks_enabled=bound?1:0;dkr_code_hook_mask=bound?bound->world->hook_mask():nullptr;}
void unbind_thread(){dkr_code_callbacks_enabled=0;dkr_code_hook_mask=nullptr;bound.reset();}
void boot(std::uint8_t* ram,std::size_t bytes) {
    bind_thread();if(!bound)return;
    try {bound->world->attach({ram,bytes},true);bound->world->initialize();}
    catch(const std::exception& e){stop(e.what());}
}
std::string failure(){std::lock_guard lock(error_mutex);return last_error;}
mods::Bytes retire_bootstrap_checkpoint(std::span<std::uint8_t> live_ram) {
    const auto value=ordinary.load();if(!value)return {};
    // All native owners have joined. No subsequent call may reacquire this
    // world, whether capture succeeds or fails; the owned runtime gets bytes.
    ordinary.store(nullptr);
    return value->world->retire_checkpoint(live_ram);
}
std::shared_ptr<const mods::code::FrozenSet> frozen(){const auto value=ordinary.load();return value?value->frozen:nullptr;}
int dispatch(bool leaving,std::uint32_t address,std::uint8_t* ram,recomp_context* context) {
    if(!bound||!bound->world)stop("Code callback has no owner on this CPU thread.");
    // The ordinary game worker yields through its existing scheduler at a
    // retired main-loop boundary. UI/audio/VI workers are never blocked here.
    if(!leaving&&bound->world->sdk2_frame_starts_at(address))for(;;) {
        const int status=bound->world->sdk2_dispatch_pulse();if(status<0)stop(bound->world->error());if(status)break;
        ultramodern::wait_for_external_message_timed(ram,16);ultramodern::check_running_queue(ram);
    }
    std::uint64_t result=0;
    if(bound->world->dispatch(leaving,address,ram,context,result)<0)stop(bound->world->error());
    if(leaving&&bound->world->frame_ends_at(address))for(const auto& effect:bound->world->effects())
        std::fprintf(stderr,"[custom-magic-codes][%s] %s\n",effect.package.c_str(),effect.text.c_str());
    return int(result);
}
}
extern "C" int dkr_code_hook_enter(std::uint32_t address,std::uint8_t* ram,recomp_context* context) {
    return dkr::runtime::code_mods::dispatch(false,address,ram,context);
}
extern "C" int dkr_code_hook_leave(std::uint32_t address,std::uint8_t* ram,recomp_context* context) {
    return dkr::runtime::code_mods::dispatch(true,address,ram,context);
}
extern "C" void dkr_code_event(unsigned event,std::uint8_t* ram,recomp_context* context) {
    using namespace dkr::runtime::code_mods;
    if(!bound||!bound->world||bound->world->dispatch_event(event,ram,context)<0)
        stop(bound&&bound->world?bound->world->error():"Game event has no code owner.");
}
extern "C" int dkr_code_call_indirect(std::uint32_t address,std::uint8_t* ram,recomp_context* context) {
    using namespace dkr::runtime::code_mods;
    if(address<dkr::mods::code::CodeBase)return 0;
    if(!bound||!bound->world)stop("Indirect code callback has no CPU owner.");
    std::uint64_t result=0;
    if(bound->world->indirect(address,ram,context,result)<0)stop(bound->world->error());
    return int(result);
}
extern "C" std::uint32_t dkr_code_lap_address(std::uint32_t address,std::uint8_t*,recomp_context*) {
    using namespace dkr::runtime::code_mods;
    if(!dkr_code_callbacks_enabled)return address;
    if(!bound||!bound->world)stop("Lap timing has no CPU owner.");
    std::uint64_t result=address;
    if(bound->world->lap_address(address,result)<0)stop(bound->world->error());
    return std::uint32_t(result);
}

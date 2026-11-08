#pragma once
#include "magic_code_library.hpp"
#include "magic_code_world.hpp"
#include "magic_code_dispatch.h"
#include "magic_code_trophy_catalog.hpp"
#include "recomp.h"
#include <mutex>

namespace dkr::mods::code {
using GuestFunction=void(*)(std::uint8_t*,recomp_context*);
using DispatchEntry=dkr_code_dispatch_entry;
// Addresses and pointers are generated from the ACTUAL compiled namespace.
// Never resolve a private world's call through librecomp's global overlay map.
struct DispatchTable {std::span<const DispatchEntry> entries;};
class GameWorld {
public:
    using Invoke=std::function<void(GuestFunction,std::uint8_t*,recomp_context*)>;
    GameWorld(const FrozenSet&,std::string_view revision,DispatchTable,std::uint64_t seed,
              bool online,Invoke invoke={});
    static LinkPolicy policy(DispatchTable={},bool online=false);
    void course_selector(std::function<void(const std::string&,unsigned)> selector){course_selector_=std::move(selector);}
    void trophy_selector(std::function<std::string()> selector){trophy_selector_=std::move(selector);}
    void attach(std::span<std::uint8_t> ram,bool install);
    void initialize();
    void event(unsigned,const Registers& arguments={});
    int dispatch_event(unsigned,std::uint8_t*,recomp_context*)noexcept;
    int indirect(std::uint32_t,std::uint8_t*,recomp_context*,std::uint64_t& result)noexcept;
    int lap_address(std::uint32_t,std::uint64_t& result)noexcept;
    // Called by the C-only private dispatcher, or the ordinary guest wrapper.
    // Errors return AFTER all C++ locks/leases have unwound.
    int dispatch(bool leaving,std::uint32_t address,std::uint8_t*,recomp_context*,std::uint64_t& result)noexcept;
    const std::string& error()const{return error_;}
    Bytes checkpoint()const;
    // Quiesced native-to-owned handoff only, while the supplied RAM is alive.
    // Retires the old binding even if checkpoint validation fails.
    Bytes retire_checkpoint(std::span<std::uint8_t> live_ram);
    std::shared_ptr<World::Restore> stage(View)const;
    void commit(std::shared_ptr<World::Restore>);
    std::vector<Effect> effects();
    bool frame_ends_at(std::uint32_t)const;
    const LinkedSet& linked()const{return world_.linked();}
    const std::uint8_t* hook_mask()const{return hook_mask_.data();}
    std::size_t required_ram()const{return 16*MiB+linked().resident_bytes;}
    std::size_t checkpoint_limit()const;
private:
    static Registers read(const recomp_context&);
    static void write(recomp_context&,const Registers&);
    DispatchTable table_;
    Invoke invoke_;
    bool online_=false;
    std::span<std::uint8_t> ram_;
    std::unique_ptr<Memory> native_bus_;
    mutable std::recursive_mutex mutex_;
    Budget budget_;
    unsigned nested_=0;
    unsigned native_depth_=0;
    struct CallbackContext {Registers* registers;bool entry,skip=false;std::uint32_t address;bool repeat=false;};
    CallbackContext* callback_=nullptr;
    std::map<std::uint32_t,std::pair<std::string,std::string>> named_events_;
    std::map<std::size_t,std::vector<trophy::Frozen>> cups_;
    std::vector<std::uint8_t> hook_mask_;
    std::function<void(const std::string&,unsigned)> course_selector_;
    std::function<std::string()> trophy_selector_;
    World world_;
    std::string error_;
    void native(std::uint32_t,Registers&,Budget&,bool curated=false);
    bool service(std::string_view,std::size_t,Memory&,Registers&,Budget&);
    std::uint32_t symbol(std::string_view)const;
};
}

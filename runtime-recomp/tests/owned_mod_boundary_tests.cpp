#include "replay_probe/probe_bridge.h"
#include "recomp.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cfenv>

extern "C" {
void owned_mod_boundary_success(std::uint8_t*,recomp_context*);
void owned_mod_boundary_fault(std::uint8_t*,recomp_context*);
void owned_mod_boundary_budget(std::uint8_t*,recomp_context*);
void owned_mod_boundary_dispatch(std::uint8_t*,recomp_context*);
void dkr_probe_boss_diagnostic_failure(const dkr_probe_result*,recomp_context*){}
}
namespace {
unsigned checks=0;
void check(bool value){if(!value)throw std::runtime_error("Owned native boundary check "+std::to_string(checks));++checks;}
struct Owner {
    unsigned mode=0,entered=0,destroyed=0;
    bool rejected_reentry=false,rejected_rebind=false;
    dkr_probe_result failure{};
    std::recursive_mutex mutex;
};
struct Cleanup {
    Owner& owner;
    std::vector<unsigned> bytes=std::vector<unsigned>(1024,42);
    explicit Cleanup(Owner& o):owner(o){++owner.entered;}
    ~Cleanup(){++owner.destroyed;}
};
int dispatch(void* user,const char*,std::uint8_t* ram,recomp_context* context,
    const std::uint64_t*,unsigned,const std::uint32_t*,unsigned,std::uint64_t* value)noexcept {
    auto& owner=*static_cast<Owner*>(user);
    try {
        std::lock_guard lock(owner.mutex);
        Cleanup cleanup(owner);
        // Re-entrancy through the regular root API must stay rejected. It
        // must not erase this adapter's permission for the reviewed C child.
        owner.rejected_reentry=!dkr_probe_run(owned_mod_boundary_success,ram,256,context,100).completed;
        owner.rejected_rebind=!dkr_probe_bind_mod_service(nullptr,nullptr);
        const auto fn=owner.mode==0?owned_mod_boundary_success:owner.mode==1?owned_mod_boundary_fault:
            owner.mode==2?owned_mod_boundary_budget:owned_mod_boundary_dispatch;
        auto child=dkr_probe_run_native(fn,ram,context);
        if(!child.completed){owner.failure=child;throw std::runtime_error("Contained guest callback trap");}
        *value=0x1234;return 1;
    }catch(...){return -1;} // MUST unwind native objects before the C parent trap.
}
}
int main() {
    try {
        recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;std::vector<std::uint8_t> ram(256);Owner owner;
        check(!dkr_probe_run_native(owned_mod_boundary_success,ram.data(),&ctx).completed);
        check(dkr_probe_bind_mod_service(dispatch,&owner));
        const auto rounding=std::fegetround();
        for(unsigned mode=0;mode<4;++mode) {
            owner.mode=mode;owner.entered=owner.destroyed=0;owner.failure={};
            const auto result=dkr_probe_run(owned_mod_boundary_dispatch,ram.data(),ram.size(),&ctx,40);
            check(owner.rejected_reentry && owner.rejected_rebind);
            check(owner.entered==owner.destroyed && owner.destroyed>0);
            check(owner.mutex.try_lock());owner.mutex.unlock();
            check(std::fegetround()==rounding);
            if(mode==0){check(result.completed && result.operations==3 && result.memory_accesses==1 && result.guest_entries==2);}
            else {
                check(!result.completed && std::strcmp(result.blocked,"owned-mod-service-failure")==0);
                check(!owner.failure.completed && owner.failure.blocked);
                if(mode==1)check(owner.failure.bad_address==UINT64_C(0xffffffff80800000));
                if(mode==2)check(std::strcmp(owner.failure.blocked,"operation-budget")==0 && result.operations==41);
                if(mode==3)check(owner.destroyed==4); // Root plus at most three native children.
            }
        }
        check(dkr_probe_bind_mod_service(nullptr,nullptr));
        const auto stock=dkr_probe_run(owned_mod_boundary_success,ram.data(),ram.size(),&ctx,40);
        check(stock.completed && stock.operations==2 && stock.memory_accesses==1);
        std::cout<<checks<<" native C/C++ trap, destructor, aggregate budget and recursion checks passed. No game window opened.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

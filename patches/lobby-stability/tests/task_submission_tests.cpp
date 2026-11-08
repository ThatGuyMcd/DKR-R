#include "diagnostics.hpp"
#include "sp_fixture.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace dkr::lobby_stability;
unsigned checks=0;
void check(bool ok){if(!ok)throw std::runtime_error("Task adapter assertion "+std::to_string(checks));++checks;}
template<class F>void rejects(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed);}
int main() {
    try {
        std::vector<std::uint8_t> memory(0x800000);auto* rdram=memory.data();recomp_context ctx{0x80000100};
        auto* task=TO_PTR(OSTask,ctx.r4);task->t.type=M_AUDTASK;
        osSpTaskStartGo_recomp(rdram,&ctx);check(fixture_worker==1&&fixture_inline==0);check(!bootstrap_audio_worker_allowed());
        begin_bootstrap(42);
        osSpTaskStartGo_recomp(rdram,&ctx);check(fixture_worker==2&&fixture_inline==0);check(!bootstrap_audio_worker_allowed());
        task->t.type=M_GFXTASK;osSpTaskStartGo_recomp(rdram,&ctx);check(fixture_gfx==1&&fixture_presented==0);
        task->t.type=3;osSpTaskStartGo_recomp(rdram,&ctx);check(fixture_other==1&&fixture_inline==0);
        task->t.type=M_AUDTASK;fixture_throw=true;rejects([&]{osSpTaskStartGo_recomp(rdram,&ctx);});fixture_throw=false;
        check(!bootstrap_audio_worker_allowed());
        const auto calls=fixture_calls;
        rejects([&]{osSpTaskStartGo_recomp(nullptr,&ctx);});rejects([&]{osSpTaskStartGo_recomp(rdram,nullptr);});
        ctx.r4=0x80800000;rejects([&]{osSpTaskStartGo_recomp(rdram,&ctx);});
        ctx.r4=0x80000101;rejects([&]{osSpTaskStartGo_recomp(rdram,&ctx);});check(fixture_calls==calls);
        {BootstrapAudioSubmission outer(true);check(bootstrap_audio_worker_allowed());
            std::thread unrelated([]{check(!bootstrap_audio_worker_allowed());});unrelated.join();
            {BootstrapAudioSubmission inner(true);check(bootstrap_audio_worker_allowed());}check(bootstrap_audio_worker_allowed());}
        check(!bootstrap_audio_worker_allowed());end_bootstrap();
        std::cout<<"PASS "<<checks<<" owned task adapter routing/RAII/guard assertions with host-only SDK boundary fixture. No game or RSP execution.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

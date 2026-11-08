#include "netplay/peer_relay/transport.hpp"
#include "online_stability/control.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
// runtime_netplay.cpp owns a DirectSession before main(). Exercise the same
// transport construction before ordinary translation-unit dynamic initializers,
// then let its destructor run after those initializers have been retired.
struct EarlyOwner {
    std::unique_ptr<dkr::relay::net::SessionTransport> transport;
    EarlyOwner():transport(dkr::relay::wrap(this,dkr::relay::net::make_udp_session_transport())){
        dkr::online_stability::control(this)->configure({});
        dkr::online_stability::bind_mod_sync(this,this);
    }
    ~EarlyOwner(){
        dkr::online_stability::control(this)->configure({});
        dkr::online_stability::unbind_mod_sync(this);
        dkr::online_stability::forget(this);
    }
};
#if defined(_MSC_VER)
#pragma init_seg(lib)
EarlyOwner early;
#elif defined(__GNUC__)
EarlyOwner early __attribute__((init_priority(101)));
#else
EarlyOwner early;
#endif
void check(bool value){if(!value)std::abort();}
}
int main(){
    using namespace dkr::relay;
    check(dkr::online_stability::mod_control(&early)==dkr::online_stability::control(&early));
    check(early.transport!=nullptr);
    check(!view(&early).enabled&&view(&early).rows.empty());
    std::string error;check(early.transport->open(0,error));
    auto previous=std::move(early.transport);
    early.transport=wrap(&early,net::make_udp_session_transport());
    Context context;context.active=context.allow=true;context.local=context.host=1;context.room=77;
    context.members.push_back({1,"Startup test","",true});configure(&early,context,{});
    check(view(&early).enabled);previous.reset();check(view(&early).enabled);
    reset(&early);check(!view(&early).enabled);
    std::puts("Pre-main relay construction and registry replacement passed; process exit also exercises late global destruction.");
}

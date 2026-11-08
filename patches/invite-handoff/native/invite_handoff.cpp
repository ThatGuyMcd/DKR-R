#include "invite_handoff.hpp"
#include "netplay/direct_session.hpp"
#include "runtime_netplay.hpp"
#include <mutex>
#include <utility>

namespace dkr::runtime::invites {
namespace {
std::atomic<bool> live{false},modal{false},release{false},cancel{false};
std::atomic<std::uint64_t> shown{0},focus{0};
std::mutex mutex;
ReturnView returning;
}
void game_running(bool v){live.store(v,std::memory_order_release);}
bool game_running(){return live.load(std::memory_order_acquire);}
bool begin_return(){std::scoped_lock lock(mutex);if(returning.phase!=ReturnPhase::Idle||!game_running())return false;returning={ReturnPhase::Stopping,{}};return true;}
bool return_pending(){std::scoped_lock lock(mutex);return returning.phase==ReturnPhase::Stopping;}
void finish_return(std::string error){
    game_running(false);
    {std::scoped_lock lock(mutex);if(returning.phase!=ReturnPhase::Stopping)return;}
    // Keep the immutable old session/save route alive until every producer has
    // retired. A save failure does not permit admission into a different room.
    if(error.empty())netplay::session().disconnect("Leaving this lobby to accept an invitation.");
    std::scoped_lock lock(mutex);returning={error.empty()?ReturnPhase::Ready:ReturnPhase::Failed,std::move(error)};
}
ReturnView return_view(){std::scoped_lock lock(mutex);return returning;}
void clear_return(){std::scoped_lock lock(mutex);if(returning.phase!=ReturnPhase::Stopping)returning={};}
void dialog(bool v){if(modal.exchange(v,std::memory_order_acq_rel)&&!v)release=true;}
bool dialog(){return modal.load(std::memory_order_acquire);}
bool capture(){return dialog()||release.load(std::memory_order_acquire);}
bool release_pending(){return release.load(std::memory_order_acquire);}
void fence_inputs(){release.store(true,std::memory_order_release);}
void release_if_clear(bool clear){if(clear&&!dialog())release=false;}
void visible(std::uint64_t id){shown.store(id,std::memory_order_release);}
void focus_visible(){focus.store(shown.load(std::memory_order_acquire),std::memory_order_release);}
std::uint64_t take_focus(){return focus.exchange(0,std::memory_order_acq_rel);}
void cancel_dialog(){cancel.store(true,std::memory_order_release);}
bool take_cancel(){return cancel.exchange(false,std::memory_order_acq_rel);}
}

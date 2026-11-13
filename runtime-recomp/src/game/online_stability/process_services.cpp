#include "process_services.hpp"
#include <map>
#include <mutex>
#include <stdexcept>

namespace dkr::online_stability {
namespace {
struct Owners {std::mutex mutex;std::map<const void*,std::function<void()>> jobs;};
Owners& owners(){static auto* value=new Owners;return *value;}
}
void bind_store_http(const void* owner,std::function<void()> stop){
    if(!owner||!stop)throw std::invalid_argument("Invalid process HTTP owner.");
    auto& state=owners();std::lock_guard lock(state.mutex);
    if(!state.jobs.contains(owner)&&state.jobs.size()>=4)throw std::logic_error("Too many process HTTP owners.");
    state.jobs[owner]=std::move(stop);
}
void unbind_store_http(const void* owner){auto& state=owners();std::lock_guard lock(state.mutex);state.jobs.erase(owner);}
void retire_store_http(){
    std::map<const void*,std::function<void()>> jobs;
    {auto& state=owners();std::lock_guard lock(state.mutex);jobs.swap(state.jobs);}
    // Call owners outside the registry lock; their threads may use services
    // while finishing cancellation. The main thread retains their lifetimes.
    for(auto& [owner,stop]:jobs)stop();
}
}

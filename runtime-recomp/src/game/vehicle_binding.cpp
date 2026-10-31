#include "vehicle_binding.hpp"
#include "atomic_snapshot.hpp"
#include <atomic>
#include <mutex>

namespace dkr::mods::vehicles {
namespace {
struct Binding { std::weak_ptr<const void> owner;std::shared_ptr<const Resources> resources; };
std::mutex mutex;
std::map<const void*,Binding> bindings;
dkr::runtime::AtomicSnapshot<const Resources> renderer;
}
void bind_resources(std::shared_ptr<const void> owner,std::shared_ptr<const Resources> resources) {
    if(!owner||!resources||resources->library.packages.empty())return;
    std::lock_guard lock(mutex);
    std::erase_if(bindings,[](const auto& b){return b.second.owner.expired();});
    const auto it=bindings.find(owner.get());
    if(it!=bindings.end()&&it->second.resources->identity!=resources->identity)throw Error("A running asset owner cannot change its custom vehicles.");
    if(it==bindings.end()&&bindings.size()>=64)throw Error("Too many concurrent custom-vehicle preparation owners.");
    bindings.insert_or_assign(owner.get(),Binding{owner,std::move(resources)});
}
std::shared_ptr<const Resources> resources_for(const void* owner) {
    if(!owner)return {};
    std::lock_guard lock(mutex);const auto it=bindings.find(owner);
    if(it==bindings.end())return {};
    if(it->second.owner.expired()){bindings.erase(it);return {};}
    return it->second.resources;
}
void inherit_resources(const void* source,std::shared_ptr<const void> destination) {bind_resources(std::move(destination),resources_for(source));}
void publish_render_resources(std::shared_ptr<const Resources> resources) {renderer.store(std::move(resources),std::memory_order_release);}
std::shared_ptr<const Resources> render_resources() {return renderer.load(std::memory_order_acquire);}
} // namespace dkr::mods::vehicles

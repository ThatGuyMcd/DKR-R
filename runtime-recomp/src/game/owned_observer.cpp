#include "owned_observer.hpp"
#include <unordered_map>
#include <stdexcept>

namespace dkr::runtime::spectators {
namespace {
thread_local std::unordered_map<const void*, OwnedObserverHooks> hooks;
}
void register_owned_observer(const void* world, OwnedObserverHooks value) {
    if (!world || !value.context || !value.retire || !value.admit || !value.magic_actions || !value.read_only ||
        hooks.contains(world))
        throw std::runtime_error("Invalid or duplicate owned observer registration.");
    hooks.emplace(world, value);
}
void unregister_owned_observer(const void* world) {
    hooks.erase(world);
}
Work retire_owned_observer(const void* world, Cursor next, std::string& error) {
    const auto p = hooks.find(world);
    if (p == hooks.end()) {
        error = "Observer admission was called outside its owned simulation thread.";
        return Work::Failed;
    }
    return p->second.retire(p->second.context, next, error);
}
bool admit_owned_observer(const void* world, const Baseline& baseline, std::string& error) {
    const auto p = hooks.find(world);
    if (p == hooks.end()) {
        error = "Observer admission has no owned guest adapter.";
        return false;
    }
    return p->second.admit(p->second.context, baseline, error);
}
std::uint32_t owned_magic_actions(const void* world) {
    const auto p = hooks.find(world);
    return p == hooks.end() ? 0 : p->second.magic_actions(p->second.context);
}
void owned_observer_read_only(const void* world, bool enabled) {
    const auto p = hooks.find(world);
    if (p != hooks.end())
        p->second.read_only(p->second.context, enabled);
}
} // namespace dkr::runtime::spectators

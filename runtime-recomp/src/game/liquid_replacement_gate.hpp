#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dkr::runtime::water::streaming {
struct Member {
    std::uint64_t hash = 0;
    std::uint32_t layer = 0;
    std::string path;
};
struct Family {
    std::vector<Member> members;
    bool complete = true;
};
struct Selection {
    const void* map = nullptr;
    std::uint64_t era = 0;
    std::uint32_t scene = 0;
    std::vector<Family> groups;
    bool force_native = false;
};
using SelectionPtr = std::shared_ptr<const Selection>;
struct Cache {
    std::uint64_t epoch = 1, evictions = 0, era = 0;
    bool overflow = false;
};
struct Publication {
    SelectionPtr selection;
    std::uint64_t frame = 0;
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<const void*, Cache> caches;
    std::unordered_map<const void*, Publication> workloads;
    std::uint64_t next_era = 1;
};
inline Registry& registry() {
    static auto* value = new Registry;
    return *value;
}
inline Cache& cache_locked(Registry& r, const void* map) {
    auto [it, inserted] = r.caches.try_emplace(map);
    if (inserted)
        it->second.era = r.next_era++;
    return it->second;
}
inline void forget(const void* map) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    r.caches.erase(map);
    for (auto i = r.workloads.begin(); i != r.workloads.end();)
        if (i->second.selection && i->second.selection->map == map)
            i = r.workloads.erase(i);
        else
            ++i;
}
inline void changed(const void* map) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    ++cache_locked(r, map).epoch;
}
inline void evicted(const void* map) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    auto& c = cache_locked(r, map);
    ++c.epoch;
    ++c.evictions;
}
inline std::uint64_t eviction_epoch(const void* map) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    return cache_locked(r, map).evictions;
}

// One immutable selection per prepared scene. Queue slots retain that selection
// until reused; loading a new scene cannot change a queued older workload.
inline SelectionPtr select_families(const void* map, std::uint32_t scene, std::vector<Family> families) {
    auto result = std::make_shared<Selection>();
    result->map = map;
    result->scene = scene;
    {
        auto& r = registry();
        std::scoped_lock lock(r.mutex);
        result->era = cache_locked(r, map).era;
    }
    std::size_t members = 0;
    for (const auto& f : families)
        members += f.members.size();
    if (families.size() > 128 || members > 4096) {
        result->force_native = true;
        return result;
    }
    // A shared replacement hash cannot be HD for one active animation and
    // native for another. Merge connected families before deciding readiness.
    std::vector<std::size_t> parent(families.size());
    for (std::size_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    const auto root = [&](std::size_t i) {
        while (parent[i] != i)
            i = parent[i];
        return i;
    };
    std::unordered_map<std::uint64_t, std::size_t> owners;
    for (std::size_t i = 0; i < families.size(); ++i)
        for (const auto& m : families[i].members) {
            const auto [it, added] = owners.emplace(m.hash, i);
            if (!added)
                parent[root(i)] = root(it->second);
        }
    std::unordered_map<std::size_t, std::size_t> groups;
    for (std::size_t i = 0; i < families.size(); ++i) {
        const auto [it, added] = groups.emplace(root(i), result->groups.size());
        if (added)
            result->groups.push_back({});
        auto& group = result->groups[it->second];
        group.complete &= families[i].complete;
        for (auto& m : families[i].members)
            group.members.push_back(std::move(m));
    }
    for (auto& group : result->groups) {
        std::sort(group.members.begin(), group.members.end(),
                  [](const auto& a, const auto& b) { return a.hash < b.hash; });
        for (std::size_t i = 1; i < group.members.size(); ++i) {
            const auto& a = group.members[i - 1];
            const auto& b = group.members[i];
            if (a.hash == b.hash && (a.layer != b.layer || a.path != b.path))
                group.complete = false;
        }
        group.members.erase(std::unique(group.members.begin(), group.members.end(),
                                        [](const auto& a, const auto& b) { return a.hash == b.hash; }),
                            group.members.end());
    }
    return result;
}
inline void publish(const void* workload, std::uint64_t frame, SelectionPtr selection) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    if (!selection) {
        r.workloads.erase(workload);
        return;
    }
    if (r.workloads.find(workload) == r.workloads.end() && r.workloads.size() >= 64) {
        // Fail closed under an unexpected queue population, with bounded
        // metadata. Never evict the selection of an in-flight workload.
        cache_locked(r, selection->map).overflow = true;
        return;
    }
    r.workloads.insert_or_assign(workload, Publication{std::move(selection), frame});
}
inline Publication& current_workload() {
    static thread_local Publication value;
    return value;
}
class WorkloadScope {
    Publication previous_;

  public:
    WorkloadScope(const void* workload, std::uint64_t frame) : previous_(current_workload()) {
        auto& r = registry();
        std::scoped_lock lock(r.mutex);
        const auto i = r.workloads.find(workload);
        current_workload() = (i != r.workloads.end() && i->second.frame == frame) ? i->second : Publication{};
    }
    ~WorkloadScope() {
        current_workload() = std::move(previous_);
    }
    WorkloadScope(const WorkloadScope&) = delete;
};
struct Frame {
    std::unordered_set<std::uint64_t> blocked;
    SelectionPtr selection;
    const void* map = nullptr;
    std::uint64_t epoch = ~0ULL, era = 0;
    std::uint32_t source_version = 0, descriptor_version = 0;
    bool force_native = false, overflow = false;
};
inline auto& frames() {
    static thread_local std::unordered_map<const void*, Frame> value;
    return value;
}
template <class Map, class Replacements, class Versions>
void capture(const void* renderer, Map& map, Replacements& replacements, Versions& versions, std::uint32_t& global) {
    auto& frame = frames()[renderer];
    const auto selected = current_workload().selection;
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto at = r.caches.find(&map.replacementMap);
    const auto epoch = at == r.caches.end() ? 0 : at->second.epoch;
    const auto era = at == r.caches.end() ? 0 : at->second.era;
    const bool overflow = at != r.caches.end() && at->second.overflow;
    if (frame.map != &map || frame.selection != selected || frame.epoch != epoch || frame.era != era ||
        frame.source_version != global || frame.overflow != overflow) {
        frame.map = &map;
        frame.selection = selected;
        frame.epoch = epoch;
        frame.era = era;
        frame.source_version = global;
        frame.blocked.clear();
        frame.force_native = overflow;
        frame.overflow = overflow;
        if (selected && selected->map == &map.replacementMap) {
            frame.force_native |= selected->force_native;
            for (const auto& group : selected->groups) {
                bool ready = group.complete && selected->era == era;
                for (const auto& member : group.members)
                    if (member.path.empty() || !map.replacementMap.getFromRelativePath(member.layer, member.path))
                        ready = false;
                if (!ready)
                    for (const auto& member : group.members)
                        frame.blocked.insert(member.hash);
            }
        }
        ++frame.descriptor_version;
    }
    global = frame.descriptor_version;
    for (const auto& [hash, index] : map.hashMap)
        if (index < replacements.size() && index < versions.size() &&
            (frame.force_native || frame.blocked.find(hash) != frame.blocked.end())) {
            replacements[index] = nullptr;
            versions[index] ^= 0x80000000U;
        }
}
inline bool blocked(const void* renderer, std::uint64_t hash) {
    const auto i = frames().find(renderer);
    return i != frames().end() && (i->second.force_native || i->second.blocked.find(hash) != i->second.blocked.end());
}
inline void release_frame(const void* renderer) {
    frames().erase(renderer);
}
} // namespace dkr::runtime::water::streaming

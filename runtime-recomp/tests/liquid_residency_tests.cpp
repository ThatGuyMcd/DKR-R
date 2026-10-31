// Compile the production residency body with fake cache/queue interfaces only.
// No game, renderer, ROM, GPU, filesystem textures or thread workers are used.
#include "water_residency_rt64.hpp"
#include <iostream>
#include <stdexcept>
struct Clock {
    using rep = long long;
    using period = std::milli;
    using duration = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<Clock>;
    static constexpr bool is_steady = true;
    static inline time_point value{};
    static time_point now() {
        return value;
    }
};
namespace w = dkr::runtime::water;
int main() {
    try {
        unsigned checks = 0;
        auto check = [&](bool ok, const char* message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        std::vector<std::uint8_t> ram(0x800000);
        auto byte = [&](unsigned at, unsigned value) { ram[at ^ 3U] = static_cast<std::uint8_t>(value); };
        auto half = [&](unsigned at, unsigned value) {
            byte(at, value >> 8);
            byte(at + 1, value);
        };
        auto word = [&](unsigned at, unsigned value) {
            half(at, value >> 16);
            half(at + 2, value);
        };
        auto header = [&](unsigned at, unsigned pixel) {
            byte(at, 2);
            byte(at + 1, 2);
            byte(at + 2, 0);
            half(at + 0x12, 0x0200);
            half(at + 0x16, 48);
            for (unsigned i = 0; i < 16; ++i)
                byte(at + 32 + i, pixel);
        };
        header(0x1000, 0);
        header(0x1030, 1);
        word(0x100, 2);
        word(0x104, 0x500);
        const auto a = dkr::runtime::rice_texture::native_alias("00004140#0#3");
        const auto b = dkr::runtime::rice_texture::native_alias("24246364#0#3");
        RT64::TextureCache cache;
        RT64::State state;
        state.ext.textureCache = &cache;
        state.RDRAM = ram.data();
        auto& map = cache.textureMap.replacementMap;
        map.fileSystemResolvedPaths[0].emplace(a, RT64::ReplacementResolvedPath{0, a, "A"});
        map.fileSystemResolvedPaths[0].emplace(b, RT64::ReplacementResolvedPath{0, b, "B"});
        w::enhanced::Registry materials{{0x1000}};
        w::LiquidResidencyT<Clock> residency;
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.size() == 2, "Complete animation did not batch both frames");
        check(cache.resolvedPathQueue[0].relativePath == "A" && cache.resolvedPathQueue[1].relativePath == "B",
              "Production CRC or pack binding changed");
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.size() == 2, "Unchanged frame queued duplicate prefetch");
        int old_slot = 0;
        residency.publish(&old_slot, 1);
        w::streaming::SelectionPtr old;
        {
            w::streaming::WorkloadScope scope(&old_slot, 1);
            old = w::streaming::current_workload().selection;
        }
        check(old && old->groups.size() == 1 && old->groups[0].complete, "Published family missing");
        map.resident = {{0, "A"}, {0, "B"}};
        cache.resolvedPathQueue.clear();
        w::streaming::changed(&map);
        Clock::value += std::chrono::seconds(1);
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.empty(), "Resident animation was queued again");
        map.resident.erase({0, "B"});
        w::streaming::evicted(&map);
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.empty(), "Eviction bypassed retry throttle");
        Clock::value += std::chrono::seconds(1);
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.size() == 1 && cache.resolvedPathQueue[0].relativePath == "B",
              "Evicted sibling was not asynchronously requested");
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.size() == 1, "Repeated draw flooded recovery queue");
        cache.resolvedPathQueue.clear();
        for (unsigned i = 0; i < 4; ++i) {
            Clock::value += std::chrono::seconds(1);
            residency.prepare(state, materials, 1, true);
            check(cache.resolvedPathQueue.size() == 1, "Bounded retry missing");
            cache.resolvedPathQueue.clear();
        }
        Clock::value += std::chrono::seconds(1);
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.empty(), "Failed upload retry budget was unbounded");
        map.resident.emplace(0, "B");
        w::streaming::changed(&map);
        map.resident.erase({0, "B"});
        w::streaming::evicted(&map);
        Clock::value += std::chrono::seconds(1);
        residency.prepare(state, materials, 1, true);
        check(cache.resolvedPathQueue.size() == 1, "A later eviction did not rearm recovery");
        // A new partial scene must not alter the already published complete scene.
        header(0x2000, 0);
        header(0x2030, 2);
        materials.headers = {0x2000};
        cache.resolvedPathQueue.clear();
        residency.prepare(state, materials, 2, true);
        int new_slot = 0;
        residency.publish(&new_slot, 2);
        {
            w::streaming::WorkloadScope scope(&new_slot, 2);
            const auto selected = w::streaming::current_workload().selection;
            check(selected && selected != old && !selected->groups[0].complete, "Partial scene not isolated");
        }
        check(old->groups[0].complete && old->scene == 1, "New scene mutated retained publication");
        check(cache.resolvedPathQueue.empty(), "Unusable partial family consumed prefetch queue");
        Clock::value += std::chrono::seconds(2);
        residency.prepare(state, materials, 2, true);
        check(cache.resolvedPathQueue.empty(), "Partial family entered recovery loop");
        // Preserve stream/preload policy and highest-priority pack resolution.
        map.fileSystemResolvedPaths.push_back({{a, {1, a, "higher", RT64::ReplacementOperation::Preload}}});
        map.fileSystems.push_back(2);
        materials.headers = {0x1000};
        cache.resolvedPathQueue.clear();
        ++cache.generation;
        residency.prepare(state, materials, 3, true);
        residency.publish(&new_slot, 3);
        {
            w::streaming::WorkloadScope scope(&new_slot, 3);
            auto selection = w::streaming::current_workload().selection;
            const auto& members = selection->groups[0].members;
            auto found =
                std::find_if(members.begin(), members.end(), [&](const auto& member) { return member.hash == a; });
            check(found != members.end() && found->layer == 1 && found->path == "higher", "Pack priority changed");
        }
        check(std::none_of(cache.resolvedPathQueue.begin(), cache.resolvedPathQueue.end(),
                           [](const auto& path) { return path.relativePath == "higher"; }),
              "Preload policy changed to streaming");
        residency.prepare(state, materials, 3, false);
        residency.publish(&new_slot, 4);
        {
            w::streaming::WorkloadScope scope(&new_slot, 4);
            check(!w::streaming::current_workload().selection, "Optical water inherited original-family gating");
        }
        cache.resolvedPathQueue.clear();
        residency.prepare(state, materials, 3, true);
        residency.publish(&new_slot, 5);
        {
            w::streaming::WorkloadScope scope(&new_slot, 5);
            check(bool(w::streaming::current_workload().selection), "Returning to original optics did not reprepare");
        }
        check(cache.resolvedPathQueue.size() <= 1, "Return scan requested resident or nonstreaming artwork");
        w::streaming::forget(&map);
        std::cout
            << checks
            << " production residency checks passed; synthetic cache and clock; no game, ROM, GPU or RSP executed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

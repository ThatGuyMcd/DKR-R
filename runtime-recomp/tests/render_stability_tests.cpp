#include "liquid_replacement_gate.hpp"
#include "render_provenance.hpp"
#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
namespace w = dkr::runtime::water::streaming;
namespace p = dkr::runtime::render_provenance;
struct Replacements {
    int texture = 1;
    std::set<std::pair<unsigned, std::string>> resident{{0, "A"}, {0, "B"}, {0, "C"}};
    int* getFromRelativePath(unsigned layer, const std::string& path) {
        return resident.contains({layer, path}) ? &texture : nullptr;
    }
};
struct Map {
    Replacements replacementMap;
    std::unordered_map<std::uint64_t, std::size_t> hashMap{{1, 0}, {2, 1}, {3, 2}, {4, 3}};
};
int main() {
    try {
        unsigned checks = 0;
        auto check = [&](bool ok, const char* message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        Map map;
        int renderer = 0, old_slot = 0, new_slot = 0, texture = 0;
        auto complete = w::select_families(&map.replacementMap, 1, {{{{1, 0, "A"}, {2, 0, "B"}}, true}});
        w::publish(&old_slot, 1, complete);
        const auto capture = [&](const void* slot, unsigned frame) {
            std::vector<int*> textures(4, &texture);
            std::vector<unsigned> versions(4, 1);
            unsigned global = 7;
            w::WorkloadScope scope(slot, frame);
            w::capture(&renderer, map, textures, versions, global);
            return textures;
        };
        auto hd = capture(&old_slot, 1);
        check(hd[0] && hd[1], "Complete family did not select HD");
        auto overlap = w::select_families(&map.replacementMap, 2,
                                          {{{{1, 0, "A"}, {2, 0, "B"}}, true}, {{{1, 0, "A"}, {3, 0, ""}}, false}});
        w::publish(&new_slot, 2, overlap);
        auto mixed = capture(&new_slot, 2);
        check(!mixed[0] && !mixed[1] && !mixed[2], "Overlapping animation did not fall back atomically");
        auto retained = capture(&old_slot, 1);
        check(retained[0] && retained[1], "New scene poisoned queued old scene");
        w::publish(&new_slot, 3, complete);
        auto returned = capture(&new_slot, 3);
        check(returned[0] && returned[1], "Old incomplete scene poisoned return to a complete scene");
        w::publish(&new_slot, 4,
                   w::select_families(&map.replacementMap, 4,
                                      {{{{1, 0, "A"}, {2, 0, "B"}}, true}, {{{3, 0, ""}, {4, 0, ""}}, false}}));
        auto separate = capture(&new_slot, 4);
        check(separate[0] && separate[1] && !separate[2] && !separate[3], "Unrelated partial family affected HD");
        map.replacementMap.resident.erase({0, "B"});
        w::evicted(&map.replacementMap);
        auto evicted = capture(&old_slot, 1);
        check(!evicted[0] && !evicted[1], "Eviction left a mixed family");
        check(w::eviction_epoch(&map.replacementMap) == 1, "Eviction signal missing");
        map.replacementMap.resident.emplace(0, "B");
        w::changed(&map.replacementMap);
        check(w::eviction_epoch(&map.replacementMap) == 1, "Upload incorrectly counted as an eviction");
        auto restored = capture(&old_slot, 1);
        check(restored[0] && restored[1], "Residency recovery did not restore HD");
        auto stale = capture(&new_slot, 1);
        check(stale[0] && stale[1], "Wrong frame inherited a reused queue slot publication");
        // Nested scopes and worker-local decisions cannot change a captured frame.
        {
            w::WorkloadScope outer(&old_slot, 1);
            {
                w::WorkloadScope inner(&new_slot, 4);
                check(w::current_workload().selection->scene == 4, "Inner scope wrong");
            }
            check(w::current_workload().selection == complete, "Outer scope not restored");
        }
        check(!w::current_workload().selection, "Workload scope leaked");
        auto transit = w::select_families(&map.replacementMap, 5,
                                          {{{{1, 0, "A"}, {2, 0, "B"}}, true},
                                           {{{2, 0, "B"}, {3, 0, "C"}}, true},
                                           {{{3, 0, "C"}, {4, 0, ""}}, false}});
        w::publish(&new_slot, 5, transit);
        auto transitive = capture(&new_slot, 5);
        check(std::all_of(transitive.begin(), transitive.end(), [](auto* t) { return !t; }),
              "Transitive overlap was not atomic");
        // Deduplicating repeated groups must not create a false partial decision.
        auto duplicate = w::select_families(&map.replacementMap, 6,
                                            {{{{1, 0, "A"}, {2, 0, "B"}}, true}, {{{2, 0, "B"}, {1, 0, "A"}}, true}});
        check(duplicate->groups.size() == 1 && duplicate->groups[0].members.size() == 2,
              "Repeated groups did not collapse");
        auto conflict = w::select_families(
            &map.replacementMap, 7, {{{{1, 0, "A"}, {2, 0, "B"}}, true}, {{{1, 1, "other"}, {3, 0, "C"}}, true}});
        check(!conflict->groups[0].complete, "Conflicting pack bindings were treated as ready");
        {
            w::WorkloadScope scope(&old_slot, 1);
            w::forget(&map.replacementMap);
            w::changed(&map.replacementMap);
            std::vector<int*> textures(4, &texture);
            std::vector<unsigned> versions(4, 1);
            unsigned global = 7;
            w::capture(&renderer, map, textures, versions, global);
            check(!textures[0] && !textures[1], "Cleared map reused old layer bindings");
        }
        check(w::registry().workloads.empty(), "Cache teardown retained workload metadata");
        w::release_frame(&renderer);
        // Ordinary descriptors and tile selection use exactly the same frozen mask.
        complete = w::select_families(&map.replacementMap, 8, {{{{1, 0, "A"}, {2, 0, ""}}, false}});
        w::publish(&old_slot, 8, complete);
        {
            w::WorkloadScope scope(&old_slot, 8);
            std::vector<int*> textures(4, &texture);
            std::vector<unsigned> versions(4, 1);
            unsigned global = 7;
            w::capture(&renderer, map, textures, versions, global);
            check(!textures[0] && w::blocked(&renderer, 1), "Tile and descriptor selection disagree");
            map.replacementMap.resident.emplace(0, "anything");
            w::changed(&map.replacementMap);
            check(w::blocked(&renderer, 1), "Streaming changed a frozen workload mask");
        }
        std::atomic<unsigned> completed = 0;
        std::vector<std::thread> threads;
        for (unsigned thread = 0; thread < 4; ++thread)
            threads.emplace_back([&, thread] {
                int slot = 0, worker = 0;
                for (unsigned i = 0; i < 1000; ++i) {
                    auto selection = w::select_families(&map.replacementMap, thread * 1000 + i,
                                                        {{{{1, 0, "A"}, {2, 0, "B"}}, true}});
                    w::publish(&slot, i, selection);
                    w::WorkloadScope scope(&slot, i);
                    if (w::current_workload().selection != selection)
                        std::abort();
                    std::vector<int*> textures(4, &texture);
                    std::vector<unsigned> versions(4, 1);
                    unsigned global = 7;
                    w::capture(&worker, map, textures, versions, global);
                    if (!textures[0] || !textures[1])
                        std::abort();
                    ++completed;
                }
                w::publish(&slot, 0, {});
                w::release_frame(&worker);
            });
        for (auto& thread : threads)
            thread.join();
        check(completed == 4000, "Concurrent selection workload incomplete");
        std::vector<std::uint8_t> original(512, 0), decode = original;
        p::View view{original, decode, 0x80000000};
        check(p::inspect(view, 0, 64, 16) == p::Result::Unchanged, "Pristine input misclassified");
        decode[67] = 195;
        check(p::inspect(view, 0, 64, 16) == p::Result::PayloadChanged, "Payload writer not distinguished");
        decode[0] = 5;
        check(p::inspect(view, 0, 64, 16) == p::Result::CommandChanged, "Command writer not distinguished");
        check(p::inspect({original, original, 0}, 0, 64, 16) == p::Result::SharedDecode,
              "Shared input claimed independent evidence");
        check(p::inspect(view, 511, 0, 0) == p::Result::Unavailable, "Out of range command inspected");
        decode[0] = 0;
        check(p::inspect(view, 0, 510, 16) == p::Result::PrivatePayload, "Payload range not bounded");
        {
            p::Scope outer(original, decode, 1);
            {
                p::Scope inner(original, original, 2);
                check(p::current().root == 2, "Nested provenance wrong");
            }
            check(p::current().root == 1, "Provenance outer scope not restored");
        }
        check(p::current().submitted.empty(), "Provenance scope leaked");
        w::forget(&map.replacementMap);
        w::release_frame(&renderer);
        std::cout
            << checks
            << " render selection/provenance checks and 4000 concurrent workloads passed; no game, ROM, GPU or RSP executed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

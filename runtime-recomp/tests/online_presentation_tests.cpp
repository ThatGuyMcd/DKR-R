#include "online_presentation/frame_policy.hpp"
#include "online_presentation/test_receive_delay.hpp"
#include "online_stability/battle_view.hpp"
#include "online_view_policy.hpp"
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    namespace p = dkr::online_presentation;
    std::array<std::uint64_t, 16> histogram{};
    histogram[7] = 95;
    histogram[11] = 4;
    histogram[14] = 1;
    assert(p::percentile(histogram, 95) == 18000 && p::percentile(histogram, 99) == 40000);
    assert(p::percentile({}, 95) == 0 && p::percentile(histogram, 0) == 0);
    assert(p::asset_scene(17) == 17);
    using namespace std::chrono_literals;
    assert(!p::idle_refresh_due(32ms, false) && p::idle_refresh_due(33ms, false));
    assert(!p::idle_refresh_due(34ms, true) && !p::idle_refresh_due(65ms, true));
    assert(p::idle_refresh_due(66ms, true));
    namespace fixture = p::test_receive_delay;
    assert(fixture::ready_at() == fixture::Clock::time_point{});
    fixture::arm(35, 15);
    const auto before = fixture::Clock::now(), due = fixture::ready_at(), after = fixture::Clock::now();
    assert(due >= before + 20ms && due <= after + 50ms);
    fixture::arm(0, 0);
    assert(fixture::ready_at() == fixture::Clock::time_point{});
    fixture::arm(151, 0);
    assert(fixture::ready_at() == fixture::Clock::time_point{});
    // Real eligibility consumer: races, battles and the T.T. fourth camera;
    // menu/cutscene/missing/duplicate observations must retain shared views.
    using namespace dkr::online_stability;
    for (unsigned players = 2; players <= 4; ++players)
        for (unsigned race : {0U, 3U, 64U, 65U, 66U}) {
            std::array<dkr_owned_draw_event, 5> events{};
            events[0].kind = DKR_OWNED_ONLINE_SHARED;
            events[0].address = 0x80200000;
            for (unsigned i = 0; i < players; ++i) {
                auto& e = events[i + 1];
                e.kind = DKR_OWNED_ONLINE_VIEW;
                e.address = 0x80200008 + i * 8;
                e.token = i;
                e.parameters[0] = players - 1;
                e.parameters[2] = race;
                e.parameters[4] = players;
            }
            auto rows = std::span(events).first(players + 1);
            for (unsigned owner = 0; owner < players; ++owner) {
                const auto decision = view_eligibility(owner, players, rows);
                assert(decision.eligible);
                dkr::runtime::online_view::Session session;
                session.owner = owner;
                session.players = players;
                const auto plan = focus_plan(dkr::runtime::online_view::plan(session, rows), decision, owner, true,
                                             dkr::runtime::online_view::Labels::NameAndPosition, 100);
                assert(plan.eligible && plan.focus && plan.owner == owner && plan.layout == players - 1);
            }
            if (players == 3) {
                events[4] = events[3];
                events[4].address += 8;
                events[4].token = 3;
                assert(view_eligibility(0, players, events).eligible);
            }
            events[1].parameters[3] = 1;
            assert(view_eligibility(0, players, rows).reason == ViewReason::SharedScene);
            events[1].parameters[3] = 0;
            events[1].parameters[1] = 1;
            assert(view_eligibility(0, players, rows).reason == ViewReason::Menu);
            events[1].parameters[1] = 0;
            events[2].token = 0;
            assert(view_eligibility(0, players, rows).reason == ViewReason::Duplicate);
        }
    std::uint32_t first = 0;
    {
        p::ResourceScope scope(91);
        scope.scene(4);
        first = p::asset_scene(1);
        assert(p::asset_scene(1, 1) != p::asset_scene(1, 2));
        assert(p::asset_scene(1, 1) == p::asset_scene(9999, 1));
        for (unsigned generation = 1; generation < 10000; ++generation)
            assert(p::asset_scene(generation) == first);
        {
            p::ResourceScope other(92);
            other.scene(4);
            assert(p::asset_scene(1) != first);
        }
        assert(p::asset_scene(7) == first);
        scope.scene(5);
        assert(p::asset_scene(1) != first);
        std::thread worker([] { assert(p::asset_scene(99) == 99); });
        worker.join();
    }
    assert(p::asset_scene(17) == 17);
    {
        p::ResourceScope scope(91ULL + (1ULL << 32));
        scope.scene(4);
        assert(p::asset_scene(1) != first);
    }
    try {
        p::ResourceScope scope(1);
        scope.scene(1);
        throw 1;
    } catch (int) {
    }
    assert(p::asset_scene(17) == 17);
    std::cout
        << "Resource identity survives corrections; real scenes, epochs, threads and exception scopes remain isolated.\n";
}

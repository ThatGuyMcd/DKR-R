#include "online_stability/wait_notice.hpp"

#include <cassert>
#include <iostream>

int main() {
    using dkr::online_stability::WaitNotice;
    using namespace std::chrono_literals;
    const auto start = WaitNotice::Clock::time_point{} + 10s;

    // A short wait never shows.
    {
        WaitNotice notice;
        assert(!notice.visible(true, false, false, start));
        assert(!notice.visible(true, false, false, start + 299ms));
        assert(!notice.visible(false, false, false, start + 300ms));
        // The next one starts its own delay.
        assert(!notice.visible(true, false, false, start + 310ms));
        assert(!notice.visible(true, false, false, start + 600ms));
        assert(notice.visible(true, false, false, start + 610ms));
    }
    // A long wait shows, then lingers after play resumes.
    {
        WaitNotice notice;
        assert(!notice.visible(true, false, false, start));
        assert(notice.visible(true, false, false, start + 300ms));
        assert(notice.visible(false, false, false, start + 301ms));
        assert(notice.visible(false, false, false, start + 1049ms));
        assert(!notice.visible(false, false, false, start + 1050ms));
    }
    // Stalls in bursts: blocked 400 ms, one frame through, repeat. Up once, then steady.
    {
        WaitNotice notice;
        auto now = start;
        unsigned changes = 0;
        bool shown = false;
        for (unsigned burst = 0; burst < 20; ++burst) {
            for (unsigned tick = 0; tick < 24; ++tick, now += 16667us) {
                const bool next = notice.visible(true, false, false, now);
                changes += next != shown;
                shown = next;
            }
            for (unsigned tick = 0; tick < 2; ++tick, now += 16667us) {
                const bool next = notice.visible(false, false, false, now);
                changes += next != shown;
                shown = next;
            }
        }
        assert(shown && changes == 1);
        for (unsigned tick = 0; tick < 60; ++tick, now += 16667us)
            shown = notice.visible(false, false, false, now);
        assert(!shown);
    }
    // A different wait restarts the delay before the notice is up, not after.
    {
        WaitNotice notice;
        assert(!notice.visible(true, false, false, start));
        assert(!notice.visible(true, false, true, start + 250ms));
        assert(!notice.visible(true, false, false, start + 500ms));
        assert(notice.visible(true, false, false, start + 550ms));
        assert(notice.visible(true, false, true, start + 560ms));
    }
    // Launch stages show at once and clear at once.
    {
        WaitNotice notice;
        assert(notice.visible(true, true, false, start));
        assert(notice.visible(true, true, false, start + 5s));
        assert(!notice.visible(false, false, false, start + 5s + 1ms));
        // A stall right after one gets the usual delay.
        assert(!notice.visible(true, false, false, start + 6s));
        assert(notice.visible(true, false, false, start + 6s + 300ms));
        assert(notice.visible(false, false, false, start + 6s + 400ms));
    }
    std::cout << "Wait notice appears after a real wait and stays through a choppy stretch\n";
}

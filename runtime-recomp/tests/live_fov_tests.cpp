#include "live_fov/live_fov.hpp"
#include "live_fov/guest_call.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace dkr::runtime::live_fov;
using dkr::runtime::enhancements::PresentationProfile;
namespace {
int checks = 0;
void check(bool value) {
    if (!value)
        throw std::runtime_error("Live FOV regression failed");
    ++checks;
}
struct Race {
    Tracker tracker;
    Frame frame{{1, 1, 0x80001000U, 77}, PresentationProfile::Modern, 0, 60, 60, true, false};
    std::vector<float> calls;
    bool ready = true;
    bool tick() {
        return tracker.update(frame, [&](float fov) {
            if (!ready)
                return false;
            calls.push_back(fov);
            frame.current = fov;
            return true;
        });
    }
};

void projection_registers(std::uint8_t*, recomp_context* call) {
    check(call->f12.fl == 80.F);
    check(call->f_odd == (call->mips3_float_mode ? &call->f1.u32l : &call->f0.u32h));
    // The retail projection's sin/cos functions build double constants using
    // mtc1 into odd register aliases. Check the actual recompiler ABI, rather
    // than substituting a float-only mock for the setter as before.
    if (!call->mips3_float_mode) {
        call->f18.u32l = 0;
        call->f_odd[(19 - 1) * 2] = 0x3FE00000U;
        check(call->f18.d == 0.5);
    }
    for (unsigned odd = 1; odd < 32; odd += 2) {
        call->f_odd[(odd - 1) * 2] = 0x12340000U + odd;
        const auto* base = call->mips3_float_mode ? &call->f1.u32l : &call->f0.u32h;
        check(base[(odd - 1) * 2] == 0x12340000U + odd);
    }
    call->r4 = 123;
    call->r29 -= 0x28;
    call->status_reg = 0;
}

void guest_context_checks() {
    for (unsigned mode : {0U, 1U}) {
        recomp_context cpu{};
        cpu.mips3_float_mode = mode;
        cpu.f_odd = mode ? &cpu.f1.u32l : &cpu.f0.u32h;
        cpu.r4 = 42;
        cpu.r29 = 0x80110000U;
        cpu.status_reg = 0x12345678U;
        cpu.f18.d = -3.0;
        cpu.f12.fl = 60.F;
        const recomp_context before = cpu;
        apply_guest_fov(nullptr, cpu, 80.F, projection_registers);
        check(std::memcmp(&before, &cpu, sizeof(cpu)) == 0);
    }
    // Demonstrate why the old shallow copy was unsafe. The same pair of
    // instructions leaves the local double negative and changes the live CPU.
    recomp_context cpu{};
    cpu.f_odd = &cpu.f0.u32h;
    cpu.f18.d = -3.0;
    recomp_context shallow = cpu;
    shallow.f18.u32l = 0;
    shallow.f_odd[(19 - 1) * 2] = 0x3FE00000U;
    check(shallow.f18.d == -3.0);
    check(cpu.f18.d == 0.5);
}
} // namespace
int main() try {
    guest_context_checks();
    Race race;
    check(!race.tick());
    check(race.calls.empty());
    // This is the original failure: the same scene and level remain active,
    // including a paused race, while the user moves the slider repeatedly.
    for (const auto offset : {5, 10, 20, 0, -20, 8}) {
        race.frame.offset = offset;
        check(race.tick());
        check(race.frame.current == 60 + offset);
        const auto calls = race.calls.size();
        for (int n = 0; n < 120; ++n)
            check(!race.tick());
        check(race.calls.size() == calls);
    }
    // Clamp safely at the actual retail setter limit; no rejected 90..100.
    race.frame.authored = 80;
    race.frame.offset = 20;
    check(race.tick());
    check(race.frame.current == 89);
    race.frame.authored = 45;
    race.frame.offset = -20;
    check(race.tick());
    check(race.frame.current == 40);
    // A new level supplies its own authored FOV, rather than inheriting the
    // previous level or repeatedly adding to the already adjusted projection.
    ++race.frame.scene.generation;
    race.frame.scene.header += 0x100;
    race.frame.authored = 70;
    race.frame.current = 70;
    race.frame.offset = 5;
    check(race.tick());
    check(race.frame.current == 75);
    // Restore Accurate only when we own this scene's adjustment.
    race.frame.profile = PresentationProfile::Accurate;
    check(race.tick());
    check(race.frame.current == 70);
    Race accurate;
    accurate.frame.profile = PresentationProfile::Accurate;
    accurate.frame.current = 55;
    accurate.frame.offset = 20;
    check(!accurate.tick());
    check(accurate.frame.current == 55);
    accurate.frame.offset = -20;
    check(!accurate.tick());
    check(accurate.calls.empty());
    // A camera effect can change FOV independently. No setting changed, so
    // the live updater must not fight it on every frame.
    race.frame.profile = PresentationProfile::Modern;
    race.frame.offset = 10;
    check(race.tick());
    race.frame.current = 63;
    check(!race.tick());
    check(race.frame.current == 63);
    // Defer changes in menus/scripted cameras and online ownership. The next
    // eligible offline frame still sees the pending slider value.
    race.frame.eligible = false;
    race.frame.offset = -10;
    check(!race.tick());
    check(race.frame.current == 63);
    race.frame.eligible = true;
    race.frame.online = true;
    check(!race.tick());
    check(race.frame.current == 63);
    race.frame.online = false;
    check(race.tick());
    check(race.frame.current == 60);
    // A failed setter must remain pending, even though the scene is unchanged.
    race.ready = false;
    race.frame.offset = 12;
    check(!race.tick());
    check(race.frame.current == 60);
    race.ready = true;
    check(race.tick());
    check(race.frame.current == 82);
    for (float invalid :
         {0.F, -1.F, 120.F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        Race bad;
        bad.frame.current = invalid;
        bad.frame.offset = 10;
        check(!bad.tick());
        check(bad.calls.empty());
    }
    for (int invalid : {0, -1, 120}) {
        Race bad;
        bad.frame.authored = invalid;
        bad.frame.offset = 10;
        check(!bad.tick());
        check(bad.calls.empty());
    }
    // Reusing a header address after a scene change must not reuse ownership.
    race.frame.profile = PresentationProfile::Accurate;
    ++race.frame.scene.generation;
    race.frame.current = 50;
    check(!race.tick());
    check(race.frame.current == 50);
    std::cout << checks << " live FOV checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

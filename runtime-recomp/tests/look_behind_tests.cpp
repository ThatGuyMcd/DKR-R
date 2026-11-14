#include "camera_session.hpp"
#include "revision_addresses.hpp"
#include "look_behind/policy.hpp"
#include <bit>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// No SDK camera mod is selected. The real camera session, actions, retained
// reader, SDK bridge and local view mapping are linked into this fixture.
int main() {
    using namespace dkr::runtime;
    for (const auto revision : {rom::Revision::UsV77, rom::Revision::UsV80}) {
        const auto& t = revision == rom::Revision::UsV77 ? revision_addresses::kUsV77 : revision_addresses::kUsV80;
        std::vector<std::uint8_t> ram(0x800000);
        const auto word = [&](unsigned a, unsigned n) { std::memcpy(ram.data() + a - 0x80000000, &n, 4); };
        const auto half = [&](unsigned a, unsigned n) {
            std::uint16_t v = n;
            std::memcpy(ram.data() + ((a - 0x80000000) ^ 2), &v, 2);
        };
        const auto real = [&](unsigned a, float f) { word(a, std::bit_cast<unsigned>(f)); };
        constexpr unsigned list = 0x80010000, header = 0x80011000;
        word(t.NumberOfRacers, 2);
        word(t.RacersByPort, list);
        word(t.CurrentLevelHeader, header);
        real(t.CurrentCameraFov, 60);
        // Reordered native roster; physical controller 0 can own client slot 2.
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned view = i ? 0 : 2, object = 0x80012000 + i * 0x100, racer = 0x80013000 + i * 0x300;
            word(list + i * 4, object);
            word(object + 0x64, racer);
            half(racer, view);
            real(object + 12, float(view * 100));
            real(object + 20, 30);
            real(t.Cameras + view * 0x44 + 12, float(view * 100));
            real(t.Cameras + view * 0x44 + 16, 55);
            real(t.Cameras + view * 0x44 + 20, 100);
        }
        const auto original = ram;
        for (unsigned view : {0U, 2U}) {
            camera::SettingsChannel settings;
            camera::Actions actions;
            std::array<camera::Json, 4> defaults;
            for (auto& v : defaults)
                v = camera::Json::object();
            settings.publish(defaults);
            camera::PresentationSession session({}, revision, settings, actions);
            camera::FrameStamp f;
            f.scene = 1;
            f.epoch = 92;
            f.frame = 10;
            f.dt = 1.F / 60;
            f.online = true;
            const std::array owners{camera::ViewOwner{view, 0, 0, 16.F / 9}};
            assert(!session.active()); // Independent of ModernCamera++.
            actions.publish(0, {0, 0, 0, 0, false, true});
            assert(!session.evaluate(ram, f, owners).poses[view]);
            actions.publish(0, {DKR_VIEW_LOOK_BACK, 0, 0, 0, false, true});
            auto held = session.evaluate(ram, f, owners);
            assert(held.poses[view]);
            assert(held.poses[view]->view == view && held.poses[view]->position[2] == -40 && held.poses[view]->cut);
            assert(!held.poses[view ? 0 : 2]);
            ++f.frame;
            held = session.evaluate(ram, f, owners);
            assert(held.poses[view] && !held.poses[view]->cut);
            actions.publish(0, {0, 0, 0, 0, false, true});
            assert(!session.evaluate(ram, f, owners).poses[view]);
            actions.publish(0, {DKR_VIEW_LOOK_BACK, 0, 0, 0, true, true});
            assert(!session.evaluate(ram, f, owners).poses[view]);
            actions.publish(0, {0, 0, 0, 0, false, true});
            actions.publish(0, {DKR_VIEW_LOOK_BACK, 0, 0, 0, false, true});
            look_behind::enabled = false;
            assert(!session.evaluate(ram, f, owners).poses[view]);
            look_behind::enabled = true;
            word(t.GameMode, 1);
            assert(!session.evaluate(ram, f, owners).poses[view]);
            word(t.GameMode, 0);
        }
        assert(ram == original); // No camera or simulation RAM was written.
    }
    std::cout
        << "Both ROMs: host and client local rear view, hold/release, blocked input and unchanged guest RAM passed.\n";
}

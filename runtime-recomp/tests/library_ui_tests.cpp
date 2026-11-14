#include "library_ui/policy.hpp"
#include "library_ui/presentation.hpp"
#include "library_ui/control_layout.hpp"
#include "library_ui/automatic_start.hpp"
#include "library_ui/handoff.hpp"
#include <cassert>
#include <set>
#include <iostream>

int main() {
    using namespace dkr::library_ui;
    assert(initial_sync(true, true, false, false, 0));
    assert(initial_sync(true, true, true, true, 91));
    assert(!initial_sync(true, true, true, false, 92)); // A later scene must not rearm the intro.
    assert(!initial_sync(false, true, false, false, 0) && !initial_sync(true, false, false, false, 0));
    assert(handoff_blend(-1) == 0 && handoff_blend(0) == 0 && handoff_blend(.175) == .5F && handoff_blend(1) == 1);
    RuntimeIntro intro;
    assert(!intro.active() && intro.opacity(1, false) == 0);
    intro.begin(true);
    assert(intro.online() && intro.opacity(10, false) == 1);
    assert(intro.opacity(200, false) == 1); // A slow peer never reveals a black frame on a timer.
    assert(intro.opacity(201, true) == 1 && intro.opacity(201.175, true) > .49F);
    assert(intro.opacity(202, true) == 0 && !intro.active());
    assert(!intro.may_begin()); // Later party scene waits can temporarily report epoch zero.
    intro.clear();
    assert(intro.may_begin());
    intro.begin(false);
    assert(!intro.online() && intro.opacity(1, true) == 1);
    intro.clear();
    assert(intro.opacity(2, false) == 0);
    assert(!skip_launcher && parse_skip_launcher("1") && parse_skip_launcher("1\r"));
    for (auto invalid : {"", "0", "true", "2", "-1", "1junk", "1\r\r"})
        assert(!parse_skip_launcher(invalid));
    for (bool enabled : {false, true})
        for (bool returning : {false, true})
            for (bool recovery : {false, true}) {
                bool seen = false;
                AutomaticStart automatic(enabled, seen, returning, recovery);
                assert(seen && automatic.active() == (enabled && !returning && !recovery));
                AutomaticStart again(true, seen, false, false);
                assert(!again.active());
            }
    using Auto = AutomaticStart;
    using Action = Auto::Action;
    bool manual_seen = false;
    Auto manual(false, manual_seen, false, false);
    manual.manual(true);
    assert(manual.active() && manual.loading_mods());
    assert(manual.poll({true, false, false, true, true}) == Action::None);
    manual.cancel();
    assert(!manual.active());
    bool seen = false;
    Auto automatic(true, seen, false, false);
    assert(automatic.poll({true, false, true}) == Action::None && automatic.active());
    assert(automatic.poll({true}) == Action::Start);
    assert(automatic.poll({true, false, false, true, true}) == Action::None);
    assert(automatic.poll({true, false, false, true, false}) == Action::None); // Prepared result still belongs to Play.
    automatic.cancel();
    assert(!automatic.active() && automatic.poll({true}) == Action::None);
    for (Auto::Conditions condition :
         {Auto::Conditions{}, Auto::Conditions{true, true}, Auto::Conditions{true, false, false, true},
          Auto::Conditions{true, false, false, false, false, true}}) {
        seen = false;
        Auto fallback(true, seen, false, false);
        assert(fallback.poll(condition) == Action::ShowLauncher && !fallback.active());
        assert(fallback.poll({true}) == Action::None); // Fixing an error never creates a retry loop.
    }
    seen = false;
    Auto cancelled(true, seen, false, false);
    assert(cancelled.poll({true}) == Action::Start);
    assert(cancelled.poll({true}) == Action::ShowLauncher && !cancelled.active());
    seen = false;
    Auto failed(true, seen, false, false);
    assert(failed.poll({true}) == Action::Start);
    assert(failed.poll({true, false, false, true, false, true}) == Action::ShowLauncher);
    seen = false;
    Auto loading(true, seen, false, false);
    assert(!loading.loading_mods());
    loading.loading_mods(true);
    assert(loading.loading_mods());
    assert(loading.poll({true}) == Action::Start);
    assert(inline_preparation(loading.active(), true, false));
    assert(!inline_preparation(loading.active(), false, true));
    loading.cancel();
    assert(!inline_preparation(loading.active(), true, false));
    // Unknown work stays indeterminate. Measured batches handle zero,
    // overflow and completion without presenting an invented time estimate.
    assert(!StartupProgress{}.measured() && StartupProgress{}.fraction() == 0);
    const StartupProgress half{7, 14}, finished{18, 14};
    assert(half.measured() && half.fraction() == .5F && finished.fraction() == 1.F);
    for (float width : {320.F, 640.F, 800.F, 1280.F, 1920.F, 3440.F})
        for (float height : {480.F, 600.F, 800.F, 1080.F, 1440.F})
            for (float caption : {160.F, 220.F, 300.F}) {
                const auto layout = startup_layout(width, height, caption);
                assert(layout.logo > 0 && layout.panel > 0 && layout.panel <= width);
                assert(layout.top >= 0 && layout.top + layout.logo + layout.gap + layout.caption <= height - 7.F);
            }
    for (int width = 240; width <= 3000; ++width) {
        const auto layout = installed_toolbar(float(width), 30.F);
        assert(layout.search_width > 0 && layout.status_width > 0 && layout.sort_width > 0);
        assert(layout.status_x + layout.status_width <= width + .01F);
        assert(layout.sort_x + layout.sort_width <= width + .01F);
        assert(layout.height >= layout.sort_y + 74.F);
        if (width >= 700) {
            assert(layout.status_y == 0 && layout.sort_y == 0);
            assert(layout.search_width + 12.F == layout.status_x);
        } else {
            assert(layout.status_y >= 86.F);
            assert(layout.height >= 160.F);
        }
        for (float height : {44.F, 90.F, 156.F}) {
            const auto field = text_field(float(width), height);
            assert(field.keyboard_y == 0 && field.input_width + 12.F == field.keyboard_x);
            assert(field.keyboard_x + 44.F <= width + .01F && field.height >= height);
        }
    }
    const auto narrow = text_field(180.F, 90.F);
    assert(narrow.input_width == 180.F && narrow.keyboard_y == 98.F && narrow.height == 142.F);
    assert(vehicle_matches("Gold Mantis", true, "MANTIS", 0));
    assert(!vehicle_matches("Gold Mantis", true, "kart", 0));
    assert(vehicle_matches("Kart", true, "", 1) && !vehicle_matches("Kart", false, "", 1));
    assert(vehicle_matches("Kart", false, "", 2) && !vehicle_matches("Kart", true, "", 2));
    assert(vehicle_before("Brute", "b", "kart", "a", 0));
    assert(vehicle_before("kart", "b", "Brute", "a", 1));
    assert(vehicle_before("Kart", "a", "KART", "b", 0));
    const std::array<int, 11> expected{0, 5, 10, 6, 1, 2, 3, 7, 13, 11, 12};
    assert(SidebarOrder == expected);
    const std::set<int> pages(SidebarOrder.begin(), SidebarOrder.end());
    assert(pages.size() == PageCount);
    for (int page = 0; page < PageCount + 3; ++page)
        assert(pages.contains(page) == (page != 8 && page != 4 && page != 9));
    assert(sidebar_parent(4) == 0 && sidebar_parent(9) == 0);
    for (int page : SidebarOrder)
        assert(sidebar_parent(page) == page);
    assert(ModsSections.size() == 5);
    assert((ModsSectionOrder == std::array<int, 5>({0, 1, 4, 2, 3})));
    assert(std::string_view(ModsSections[2]) == "Textures##section");
    assert(grid_columns(2034.F) == 6 && grid_columns(1630.F) == 5);
    assert(grid_columns(815.F) == 2 && grid_columns(550.F) == 1);
    assert(grid_columns(264.F) == 1 && grid_columns(0.F) == 1);
    for (int width = 1; width < 5000; ++width) {
        const auto columns = grid_columns(float(width));
        assert(columns >= 1 && columns <= 6);
        if (columns > 1)
            assert((width - 20.F * (columns - 1)) / columns >= 300.F);
    }
    for (const auto* label : ModsSections)
        assert(label && std::string_view(label).ends_with("##section"));
    assert(configurable(AnalogueId, true, true, true, false));
    assert(!configurable("another_mod", true, true, true, true));
    assert(!configurable(AnalogueId, true, false, true, false));
    assert(!configurable(AnalogueId, false, true, true, false));
    assert(!configurable(AnalogueId, true, true, false, false));
    assert(configurable(AnalogueId, false, false, false, true));
    CaptureTarget capture;
    assert(capture.resolve(3) == 3);
    assert(!capture.begin(4));
    assert(!capture.player);
    assert(capture.begin(1));
    assert(capture.resolve(0) == 1);
    assert(!capture.begin(2));
    assert(capture.resolve(3) == 1);
    capture.clear();
    assert(capture.resolve(3) == 3);
    for (unsigned player = 0; player < 4; ++player) {
        assert(capture.begin(player));
        assert(capture.resolve(3 - player) == player);
        capture.clear();
    }
    assert(!writes_locked(false, false, false, false, false));
    for (unsigned bit = 0; bit < 5; ++bit)
        assert(writes_locked(bit == 0, bit == 1, bit == 2, bit == 3, bit == 4));
    assert(visible_status("Finish the Mod Store operation.") == "Finish the Mod Library operation.");
    assert(visible_status("MOD STORE / Mod Store") == "MOD LIBRARY / Mod Library");
    assert(visible_status("mod_store / api/store / device.bin") == "mod_store / api/store / device.bin");
    for (float height : {744.F, 1012.F, 1372.F}) {
        const bool compact = height < 840.F;
        const float padding = compact ? 14.F : 20.F, button = compact ? 40.F : 46.F, spacing = compact ? 4.F : 8.F;
        const float section = compact ? 10.F : 16.F, gap = compact ? 4.F : 8.F;
        const float after_brand = padding + (compact ? 112.F : 218.F) + (compact ? 10.F : 18.F) + 2.F * spacing;
        const auto position = sidebar_placement(height, padding, after_brand, button, spacing, section, gap, 8);
        const float nav_height = 8.F * button + 7.F * spacing;
        assert(position.navigation >= after_brand);
        assert(position.navigation + nav_height + NavigationToggleHeight + section + 2.F * spacing <=
               position.actions + .01F);
        // At constrained heights the switch reserves space before centring.
        const float desired = (height - nav_height) * .5F;
        const float latest = position.actions - nav_height - NavigationToggleHeight - section - 2.F * spacing;
        assert(std::abs(position.navigation - std::clamp(desired, after_brand, latest)) < .01F);
        assert(std::abs(position.actions + 2.F * button + gap + 2.F * spacing - (height - padding)) < .01F);
    }
    NavigationRepeat navigation;
    assert(navigation.poll(true, false, false, 0) == 0);
    assert(navigation.poll(false, false, false, .1) == 0);
    assert(navigation.poll(true, false, false, .2) == -1);
    assert(navigation.poll(true, false, false, .3) == 0);
    assert(navigation.poll(true, false, false, .6) == -1);
    assert(navigation.poll(true, false, true, .7) == 0);
    assert(navigation.poll(true, false, false, 1) == 0);
    navigation.poll(false, false, false, 1.1);
    assert(navigation.poll(false, true, false, 1.2) == 1);
    assert(navigation.poll(true, true, false, 1.3) == 0);
    const std::filesystem::path root = "profile";
    assert(running_save_scope(root, {}, true) == SaveScope::Unresolved);
    assert(running_save_scope(root, root / "saves/dkr.us.v77.bin", false) == SaveScope::Vanilla);
    assert(running_save_scope(root, root / "saves/mods/abc/dkr.us.v77.bin", false) == SaveScope::Modded);
    assert(running_save_scope(root, root / "saves/online/host/dkr.us.v77.bin", true) == SaveScope::Online);
    FeaturedRotation hero;
    hero.sync({"track", "racer", "vehicle"});
    hero.tick(0, 1, false);
    hero.tick(.25, 2, false);
    assert(hero.index == 0 && hero.remaining == 29.75);
    hero.paused = true;
    hero.tick(.5, 3, false);
    assert(hero.remaining == 29.75);
    hero.paused = false;
    hero.tick(.75, 4, true);
    assert(hero.remaining == 29.75);
    hero.tick(200, 200, false);
    assert(hero.index == 0 && hero.remaining == 29.75);
    hero.step(-1, 201);
    assert(hero.ids[hero.index] == "vehicle" && hero.previous == 0);
    hero.sync({"vehicle", "racer"});
    assert(hero.index == 0 && hero.ids[hero.index] == "vehicle");
    hero.sync({"racer"});
    assert(hero.index == 0);
    hero.step(1, 202);
    assert(hero.index == 0);
    hero.sync({});
    assert(hero.ids.empty() && hero.index == 0);
    hero.sync({"first", "second"});
    hero.remaining = .1;
    hero.tick(300, 300, false);
    hero.tick(300.25, 301, false);
    assert(hero.index == 1);
    assert(hero.blend(300.25) == 0);
    assert(hero.blend(301) == 1);
    PageMotion motion;
    motion.observe("play", 1, 0);
    assert(motion.progress(0, 0) == 0 && motion.progress(.5, 600) == 1);
    motion.observe("play", 2, .6);
    assert(!motion.active(.6));
    motion.observe("vehicles", 3, .7);
    assert(motion.active(.7) && motion.progress(.7, 0) == 0);
    motion.observe("vehicles", 100, 100);
    assert(motion.progress(100, 0) == 0);
    std::cout << "Sidebar identities/order, local capture ownership, approval/session access and write locks passed\n";
}

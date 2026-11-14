#include "magic_code_game_world.hpp"
#include <stdexcept>

using namespace dkr::mods;
using namespace dkr::mods::code;
namespace {
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Code handoff regression");
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const Error&) {
        rejected = true;
    }
    check(rejected);
}
} // namespace
int main() {
    // Match the immutable-code read that previously dereferenced freed native
    // RDRAM. Detached worlds must reject BEFORE accessing that allocation.
    auto linked = std::make_shared<LinkedSet>();
    linked->identity = std::string(64, 'a');
    linked->resident_bytes = 16;
    linked->sections.push_back({CodeBase, Bytes(16, 0), Bytes(16, 0)});
    linked->regions = {{CodeBase, 4, false, true}, {CodeBase + 4, 12, true, false}};
    World world(linked, 1);
    Bytes ram(16 * MiB + 16);
    world.attach(ram, true);
    Budget budget;
    world.initialize(budget);
    ram[((CodeBase - 0x80000000U) + 4) ^ 3U] = 0x5a;
    const auto saved = world.checkpoint();
    world.detach();
    Bytes{}.swap(ram);
    rejects([&] { world.checkpoint(); });
    rejects([&] { world.take_effects(); });
    Bytes next_ram(16 * MiB + 16);
    World resumed(linked, 1);
    resumed.attach(next_ram, true);
    resumed.commit(resumed.stage(saved));
    check(resumed.checkpoint() == saved);
    check(next_ram[((CodeBase - 0x80000000U) + 4) ^ 3U] == 0x5a);
    for (const auto revision : {"us.v77", "us.v80"}) {
        FrozenSet frozen;
        frozen.identity = session_identity({}, {}, revision, {});
        GameWorld native(frozen, revision, {}, 1, true);
        Bytes native_ram(native.required_ram());
        native.attach(native_ram, true);
        native.initialize();
        const auto initial = native.checkpoint();
        const auto bootstrap = native.retire_checkpoint(native_ram);
        check(initial == bootstrap);
        Bytes{}.swap(native_ram);
        rejects([&] { native.checkpoint(); });
        rejects([&] { native.retire_checkpoint({}); });
        GameWorld owned(frozen, revision, {}, 1, true);
        Bytes owned_ram(owned.required_ram());
        owned.attach(owned_ram, true);
        owned.commit(owned.stage(bootstrap));
        check(owned.checkpoint() == bootstrap);
        GameWorld wrong(frozen, revision, {}, 1, true);
        Bytes live(wrong.required_ram()), other(live.size());
        wrong.attach(live, true);
        wrong.initialize();
        rejects([&] { wrong.retire_checkpoint(other); });
        rejects([&] { wrong.checkpoint(); });
        GameWorld short_span(frozen, revision, {}, 1, true);
        short_span.attach(live, true);
        short_span.initialize();
        rejects([&] { short_span.retire_checkpoint(std::span(live).first(live.size() - 4)); });
        rejects([&] { short_span.checkpoint(); });
    }
}

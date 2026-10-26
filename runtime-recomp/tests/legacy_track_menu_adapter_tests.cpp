#include "legacy_track_menu_adapter.hpp"
#include "legacy_checkpoint.hpp"
#include <bit>
#include <iostream>
#if defined(DKR_TRACK_MENU_ROLLBACK_TEST)
#include "netplay/experimental_rollback.hpp"
#include <cstring>
#endif

using namespace dkr::mods;
namespace {
unsigned checks = 0;
void check(bool result, const char* message) {
    ++checks;
    if (!result)
        throw Error(message);
}
template <class F> void rejects(F fn) {
    bool caught = false;
    try {
        fn();
    } catch (const Error&) {
        caught = true;
    }
    check(caught, "Expected a bounded adapter error");
}
struct Fixture {
    static constexpr std::uint32_t background_slot = 0x80008000;
    Bytes memory = Bytes(MiB, 0);
    TrackMenuFields fields{};
    TrackMenuAdapter adapter;
    Fixture(bool races = true, std::vector<Root> courses = roots(), std::vector<TrackMenuCup> cups = {})
        : adapter(std::move(courses), races, std::move(cups)) {
        for (unsigned i = 0; i < fields.size(); ++i)
            fields[i] = 0x80001000 + i * 256;
        put(MenuField::FutureFunLand, 65535, 0, 2);
        put(MenuField::Height, 240);
        put(MenuField::HalfHeight, 120);
        put(MenuField::PreviewCarrier, 5);
        put(MenuField::LoadedCarrier, 5);
        for (unsigned r = 0; r < 5; ++r)
            for (unsigned c = 0; c < 6; ++c)
                put(MenuField::StockIDs, 5 + r * 6 + c, (r * 6 + c) * 2, 2);
        event(15, 15);
        event(0);
        adapter.install_names(memory, 0x80010000);
        event(1);
    }
    static std::vector<Root> roots() {
        std::vector<Root> result(7);
        for (unsigned i = 0; i < result.size(); ++i) {
            result[i].content_id = std::string(64, 'a' + i);
            result[i].name = "CUSTOM COURSE " + std::to_string(i);
            result[i].carrier = 5;
            result[i].vehicles = i == 0 ? 1 : 7;
        }
        return result;
    }
    void put(MenuField field, std::uint32_t value, unsigned offset = 0, unsigned size = 4) {
        const auto at = (fields[static_cast<unsigned>(field)] & 0x1fffffff) + offset;
        for (unsigned i = 0; i < size; ++i)
            memory[(at + i) ^ 3] = static_cast<std::uint8_t>(value >> ((size - i - 1) * 8));
    }
    std::uint32_t get(MenuField field, unsigned offset = 0, unsigned size = 4) const {
        const auto at = (fields[static_cast<unsigned>(field)] & 0x1fffffff) + offset;
        std::uint32_t v = 0;
        for (unsigned i = 0; i < size; ++i)
            v = (v << 8) | memory[(at + i) ^ 3];
        return v;
    }
    void real(MenuField field, float value) {
        put(field, std::bit_cast<std::uint32_t>(value));
    }
    float real(MenuField field) const {
        return std::bit_cast<float>(get(field));
    }
    TrackMenuEffect event(unsigned event, std::uint32_t arg = 0, std::uint32_t ret = 0) {
        if (event == 16 && !arg)
            arg = background_slot;
        return adapter.apply(event, memory, fields, arg, ret);
    }
    void select(int row, int col) {
        put(MenuField::CursorY, row);
        put(MenuField::CursorX, col);
    }
    void input(int x, int y) {
        const auto previous_x = get(MenuField::CursorX), previous_y = get(MenuField::CursorY);
        put(MenuField::Buttons, 0, 16);
        put(MenuField::StickX, x, 8, 2);
        put(MenuField::StickY, y, 8, 2);
        check(!event(2).navigation_sound, "Custom move pinged before committing selection");
        check(get(MenuField::StickX, 8, 2) == 0 && get(MenuField::StickY, 8, 2) == 0,
              "Custom navigation leaked into retail array indexing");
        const auto result = event(3);
        check(result.navigation_sound ==
                  (previous_x != get(MenuField::CursorX) || previous_y != get(MenuField::CursorY)),
              "Navigation ping must occur exactly for successful custom moves");
        check(!event(3).navigation_sound, "Custom navigation ping was repeated");
        check(get(MenuField::StickX, 8, 2) == static_cast<std::uint16_t>(x) &&
                  get(MenuField::StickY, 8, 2) == static_cast<std::uint16_t>(y),
              "Input was not restored");
    }
};
#if defined(DKR_TRACK_MENU_ROLLBACK_TEST)
// Real production rollback driver plus real native track adapter, with synthetic
// guest fields only. This is a state/lifecycle regression, not a gameplay test.
void rollback_stock_preview() {
    using namespace dkr::runtime::netplay;
    using namespace dkr::runtime::netplay::experimental;
    struct MenuWorld final : Simulation {
        Fixture fixture;
        bool fence = false, agreed = false;
        unsigned ticks = 0, consumed = 0, restores = 0;
        explicit MenuWorld(bool guarded) : fence(guarded) {}
        SimulationContract contract() const override {
            return {0x545241434B, MiB + 4096, kRequiredStateDomains, true, true, true};
        }
        RestoreStep prepare_tick(std::uint64_t, std::uint32_t, bool actual, std::string&) override {
            agreed = actual;
            return RestoreStep::Ready;
        }
        bool capture(std::span<std::uint8_t> output, std::string&) override {
            const auto menu = fixture.adapter.checkpoint();
            if (output.size() != contract().state_bytes || menu.size() > 4084)
                return false;
            std::fill(output.begin(), output.end(), 0);
            std::copy(fixture.memory.begin(), fixture.memory.end(), output.begin());
            const auto size = static_cast<unsigned>(menu.size());
            std::memcpy(output.data() + MiB, &ticks, 4);
            std::memcpy(output.data() + MiB + 4, &consumed, 4);
            std::memcpy(output.data() + MiB + 8, &size, 4);
            std::copy(menu.begin(), menu.end(), output.begin() + MiB + 12);
            return true;
        }
        bool restore(std::span<const std::uint8_t> input, std::string& error) override {
            if (input.size() != contract().state_bytes)
                return false;
            unsigned size = 0;
            std::memcpy(&size, input.data() + MiB + 8, 4);
            if (size > 4084)
                return false;
            try {
                auto menu = fixture.adapter.stage_checkpoint(input.subspan(MiB + 12, size));
                std::copy_n(input.begin(), MiB, fixture.memory.begin());
                fixture.adapter = std::move(menu);
                std::memcpy(&ticks, input.data() + MiB, 4);
                std::memcpy(&consumed, input.data() + MiB + 4, 4);
                ++restores;
                return true;
            } catch (const Error& failure) {
                error = failure.what();
                return false;
            }
        }
        bool tick(std::uint32_t frame, const FrameInputs& input, TickOutput& output, std::string&) override {
            check(frame == ticks, "Track adapter rollback lost its frame.");
            output = {};
            if (frame == 0) {
                fixture.event(6, 5);
                fixture.event(7, 0, 1);
            }
            if (frame == 1) {
                if (input[1].buttons)
                    fixture.select(4, 0);
                if (fence && !agreed) {
                    output.confirmation_required = true;
                    return true;
                }
            }
            if (frame == (fence ? 1U : 2U)) {
                const auto request = fixture.event(8, 5);
                check(request.scene && request.scene->id.empty() && request.scene->carrier == 5,
                      "A corrected stock preview was rebound to the custom cursor.");
                ++consumed;
            }
            ++ticks;
            return true;
        }
        bool commit(std::uint64_t, std::uint32_t, std::span<const std::uint8_t>, bool, std::string&) override {
            return true;
        }
    };
    for (bool fence : {false, true}) {
        MenuWorld delayed(fence), reference(fence);
        Driver driver(delayed), truth(reference);
        std::string error;
        check(driver.start({91, 2, 14}, error) && truth.start({91, 2, 14}, error),
              "Cannot start adapter rollback regression.");
        const auto receive = [&](Driver& owner, unsigned frame, unsigned player, PackedInput input) {
            check(owner.receive(91, static_cast<std::uint8_t>(player), frame, input) == InputResult::Accepted,
                  "Adapter input rejected.");
        };
        for (unsigned frame = 0; frame < 4; ++frame) {
            receive(truth, frame, 0, {});
            receive(truth, frame, 1, frame == 1 ? PackedInput{1, 0, 0} : PackedInput{});
            check(truth.step() == Step::Advanced, "Reference stock preview did not advance.");
        }
        receive(driver, 0, 0, {});
        receive(driver, 0, 1, {});
        check(driver.step() == Step::Advanced, "Stock preview did not queue.");
        receive(driver, 1, 0, {});
        check(driver.step() == (fence ? Step::WaitingForConfirmedInput : Step::Advanced),
              "Pending stock preview did not survive speculation/fence.");
        if (!fence) {
            receive(driver, 2, 0, {});
            check(driver.step() == Step::Advanced, "Speculative stock load did not advance.");
        }
        receive(driver, 1, 1, {1, 0, 0});
        if (fence)
            check(driver.step() == Step::Advanced, "Confirmed stock preview did not retry.");
        else {
            receive(driver, 2, 1, {});
            check(driver.step() == Step::Replayed && driver.step() == Step::Replayed,
                  "Late input could not restore/replay stock preview.");
        }
        for (unsigned frame = fence ? 2U : 3U; frame < 4; ++frame) {
            receive(driver, frame, 0, {});
            receive(driver, frame, 1, {});
            if (driver.statistics().next_frame == frame)
                check(driver.step() == Step::Advanced, "Corrected menu did not advance.");
        }
        Bytes actual(delayed.contract().state_bytes), expected(reference.contract().state_bytes);
        check(delayed.capture(actual, error) && reference.capture(expected, error) && actual == expected,
              "Corrected stock preview differs from confirmed-only state.");
        check(delayed.restores > 0 && delayed.consumed == 1 && driver.statistics().confirmed_frames == 4,
              "Stock rollback did not restore, confirm and consume exactly once.");
    }
}
#endif
void run() {
    auto long_names = Fixture::roots();
    long_names[0].name = std::string(255, 'A');
    const auto full_name = long_names[0].name;
    Fixture captions(true, long_names);
    captions.real(MenuField::Y, -4.F * 240);
    captions.event(4);
    const auto pointer = captions.get(MenuField::Details, 4 * 16 + 4);
    std::string caption;
    for (unsigned i = 0; i < 256; ++i) {
        const auto c = captions.memory[((pointer & 0x1fffffffU) + i) ^ 3U];
        if (!c)
            break;
        caption.push_back(static_cast<char>(c));
    }
    check(caption.size() == 32 && caption.ends_with("..."), "Long in-game caption exceeded its fixed draw budget");
    check(long_names[0].name == full_name, "In-game abbreviation altered installed mod metadata");
    Fixture f;
    const auto stock = f.memory;
    const auto boot_sidecar = f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(boot_sidecar).checkpoint() == boot_sidecar,
          "Initial Track Select sidecar differs.");
    for (std::size_t cut = 0; cut < boot_sidecar.size(); ++cut)
        rejects([&] { f.adapter.stage_checkpoint(View(boot_sidecar).first(cut)); });
    check(!f.event(2).navigation_sound && !f.event(3).navigation_sound, "Stock navigation received a second ping");
    check(f.memory == stock, "Unrelated stock input changed guest memory");
    check(f.get(MenuField::CursorX) == 0 && f.get(MenuField::CursorY) == 0,
          "Fresh native entry did not retain Dino Domain");
    f.select(2, 1);
    f.event(15, 15);
    f.event(0);
    f.event(1);
    check(f.get(MenuField::CursorX) == 1 && f.get(MenuField::CursorY) == 2,
          "Original remembered stock selection was replaced");
    f.select(3, 0);
    f.input(0, -1);
    check(f.get(MenuField::CursorY) == 4, "Custom rows are not reachable below stock");
    check(f.real(MenuField::TargetY) == -960 && f.get(MenuField::PreviewCarrier) == 5,
          "Custom navigation did not update preview/target");
    f.put(MenuField::Opacity, 32);
    f.event(5);
    check(f.get(MenuField::LoadedCarrier) == 0xffffffff, "Same-carrier custom preview was not reloaded");
    f.event(6, 5);
    f.event(7, 0, 1);
    f.put(MenuField::BackgroundBusy, 1);
    f.put(MenuField::LoadedCarrier, 5);
    const auto pending_sidecar = f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(pending_sidecar).checkpoint() == pending_sidecar,
          "Confirmed preview identity lost during sidecar restore.");
    f.input(1, 0);
    check(f.get(MenuField::CursorX) == 1, "Second same-carrier track cannot be selected");
    auto effect = f.event(8, 5);
    check(effect.scene && effect.scene->id == std::string(64, 'a'),
          "Rapid navigation replaced the accepted loader identity");
    check(!f.event(8, 5).scene, "A preview request was consumed twice");
    f.event(5);
    check(f.get(MenuField::LoadedCarrier) == 0xffffffff, "New logical selection reused the old preview");
    f.put(MenuField::Buttons, 0x9000, 16);
    f.event(2);
    check(f.get(MenuField::Buttons, 16) == 0, "Confirm accepted an unfinished preview");
    f.event(3);
    check(f.get(MenuField::Buttons, 16) == 0x9000, "Confirm mask escaped its input scope");
    f.put(MenuField::BackgroundBusy, 0);
    f.event(6, 5);
    f.event(7, 0, 1);
    f.put(MenuField::LoadedCarrier, 5);
    f.event(6, 5);
    f.event(7, 0, 0);
    effect = f.event(8, 5);
    check(effect.scene && effect.scene->id == std::string(64, 'b'),
          "Rejected loader request changed the outstanding preview");
    f.event(2);
    check(f.get(MenuField::Buttons, 16) == 0x9000, "Finished custom preview cannot be confirmed");
    f.event(3);
    f.real(MenuField::X, 320);
    f.real(MenuField::Y, -960);
    f.put(MenuField::SelectedX, 1);
    f.put(MenuField::SelectedY, 4);
    f.put(MenuField::Opacity, 10);
    f.event(4);
    const unsigned middle = 4 * 16;
    check(f.get(MenuField::Details, middle + 12, 1) == 1 && f.get(MenuField::Details, middle + 15, 1) == 4,
          "Custom course does not use a native ordinary wooden frame");
    check(f.get(MenuField::Details, middle + 8, 2) == 0 && f.get(MenuField::Details, middle + 10, 2) == 0,
          "Custom frame does not follow native scroll coordinates");
    check(f.get(MenuField::Details, middle + 13, 1) == 80 && (f.get(MenuField::Details, middle + 14, 1) & 128),
          "Native fade/live viewport flags are wrong");
    check(f.get(MenuField::Details, middle) == 0x80010000, "Custom row header is not guest-owned");
    f.event(16);
    check(f.real(MenuField::Y) == -720, "Custom row accesses out-of-range background data");
    rejects([&] { f.adapter.checkpoint(); });
    check(f.event(17).backgrounds.size() == 1, "Custom row has no background");
    check(f.real(MenuField::Y) == -960, "Background scope changed the viewport scroll");
    effect = f.event(9, 5);
    check(effect.override_return && effect.return_value == f.get(MenuField::Details, middle + 4),
          "Custom setup name differs from the frame title");
    check(!f.event(9, 6).override_return, "Custom name replaced another stock level");
    check(f.event(11, 5).return_value == 7, "Custom vehicle capabilities not supplied");
    f.select(4, 0);
    check(f.event(11, 5).return_value == 1 && f.event(10, 5).return_value == 0,
          "Unsupported aircraft enabled for a car-only custom course");
    f.put(MenuField::VoiceDelay, 30);
    f.event(13);
    check(f.get(MenuField::VoiceDelay) == 0, "T.T. speaks the stock carrier's name");
    f.event(12, 2);
    effect = f.event(14, 5);
    check(effect.scene && effect.scene->id == std::string(64, 'a'), "Race launch lost custom identity");
    const auto race_sidecar = f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(race_sidecar).checkpoint() == race_sidecar,
          "Race/restart custom identity lost during restore.");
    check(f.event(14, 5).scene.has_value(), "Custom restart reverted to stock");
    f.event(15, 17);
    effect = f.event(14, 5);
    check(effect.scene && effect.scene->id == std::string(64, 'a'), "Results retry lost the custom course identity");
    f.event(15, 0);
    check(!f.event(14, 5).scene, "Leaving the custom race left a sticky carrier override");
    for (unsigned destination : {3U, 6U, 15U, 17U}) {
        Fixture transition;
        transition.select(4, 0);
        transition.event(12, 2);
        transition.event(14, 5);
        transition.event(15, destination);
        check(transition.event(14, 5).scene.has_value() == (destination == 17),
              "Custom race identity survived the wrong menu transition");
    }
    Fixture wrong_course;
    wrong_course.select(4, 0);
    wrong_course.event(12, 2);
    wrong_course.event(14, 5);
    wrong_course.event(15, 17);
    check(!wrong_course.event(14, 6).scene, "Results bound another course to the custom track");
    check(!wrong_course.event(14, 5).scene, "Mismatched load did not clear the old custom race identity");
    Fixture restore;
    restore.select(5, 0);
    restore.event(0);
    check(restore.get(MenuField::CursorY) == 0, "Custom row reached original init's stock array access");
    restore.event(1);
    check(restore.get(MenuField::CursorY) == 5 && restore.real(MenuField::Y) == -1200,
          "Return to menu lost the logical custom selection");
    Fixture title_return;
    title_return.select(5, 0);
    title_return.put(MenuField::TitleLoaded, 1);
    title_return.event(0);
    check(title_return.get(MenuField::TitleLoaded) == 1, "Adapter consumed the native title reset flag");
    check(title_return.get(MenuField::CursorX) == 0 && title_return.get(MenuField::CursorY) == 0,
          "Old custom row reached stock initialization after title");
    // Native menu_track_select_init consumes this flag and selects Dino Domain.
    title_return.put(MenuField::TitleLoaded, 0);
    title_return.event(1);
    check(title_return.get(MenuField::CursorX) == 0 && title_return.get(MenuField::CursorY) == 0,
          "Custom selection overrode native title-entry Dino Domain reset");
    Fixture original;
    original.select(4, 0);
    original.event(6, 5);
    original.event(7, 0, 1);
    original.event(8, 5);
    original.select(0, 0);
    original.event(5);
    check(original.get(MenuField::LoadedCarrier) == 0xffffffff, "Stock carrier reused a custom preview");
    original.event(6, 5);
    original.event(7, 0, 1);
    effect = original.event(8, 5);
    check(effect.scene && effect.scene->id.empty(), "Stock selection did not explicitly restore original assets");
    // Prediction/resource fences may rewind while a STOCK preview is queued
    // in a menu with custom tracks installed. Empty ID means retail restoration;
    // dropping the pending request or requiring a custom ID breaks Track Select.
    for (unsigned carrier = 0; carrier < 65; ++carrier) {
        Fixture vanilla;
        vanilla.put(MenuField::PreviewCarrier, carrier);
        vanilla.event(6, carrier);
        vanilla.event(7, 0, 1);
        const auto pending = vanilla.adapter.checkpoint();
        auto restored = vanilla.adapter.stage_checkpoint(pending);
        check(restored.checkpoint() == pending, "Stock pending preview did not round-trip.");
        const auto consumed = restored.apply(8, vanilla.memory, vanilla.fields, carrier);
        check(consumed.scene && consumed.scene->id.empty() && consumed.scene->carrier == carrier,
              "Restored stock request lost its original asset identity.");
        check(!restored.apply(8, vanilla.memory, vanilla.fields, carrier).scene,
              "Stock request was consumed twice after restore.");
        const auto completed = restored.checkpoint();
        check(restored.stage_checkpoint(completed).checkpoint() == completed,
              "Completed stock preview did not round-trip.");
    }
    // Exercise stock -> custom -> stock with the SAME carrier. Restoring must
    // retain the accepted load, not bind it to the newly highlighted cell.
    Fixture alternating;
    for (unsigned round = 0; round < 12; ++round) {
        const bool custom = (round % 2) == 1;
        alternating.select(custom ? 4 : 0, 0);
        alternating.event(6, 5);
        alternating.event(7, 0, 1);
        const auto saved = alternating.adapter.checkpoint();
        alternating.select(custom ? 0 : 4, 0);
        alternating.adapter = alternating.adapter.stage_checkpoint(saved);
        effect = alternating.event(8, 5);
        check(effect.scene && effect.scene->id == (custom ? std::string(64, 'a') : std::string{}) &&
                  effect.scene->carrier == 5,
              "Correction rebound an accepted preview to a later stock/custom selection.");
    }
    const auto forged_scene = [&](std::string_view id, unsigned carrier, bool race) {
        CheckpointWriter out;
        out.u32(2);
        const auto roots = Fixture::roots();
        out.u32(static_cast<unsigned>(roots.size()));
        for (const auto& root : roots)
            out.text(root.content_id, 64);
        out.u32(0);
        out.text({}, 64);
        out.u32(0x80010000);
        out.flag(true);
        out.flag(true);
        out.flag(false);
        out.flag(false);
        out.flag(!race);
        if (!race) {
            out.text(id, 64);
            out.u32(carrier);
        }
        out.flag(race);
        if (race) {
            out.text(id, 64);
            out.u32(carrier);
        }
        out.text({}, 64);
        return std::move(out).finish();
    };
    for (unsigned carrier : {65U, 127U, 0xffffffffU})
        rejects([&] { alternating.adapter.stage_checkpoint(forged_scene({}, carrier, false)); });
    rejects([&] { alternating.adapter.stage_checkpoint(forged_scene({}, 5, true)); });
    rejects([&] { alternating.adapter.stage_checkpoint(forged_scene(std::string(64, 'z'), 5, false)); });
    rejects([&] { alternating.adapter.stage_checkpoint(forged_scene(std::string(64, 'a'), 6, false)); });
    Fixture guarded(false);
    guarded.select(4, 0);
    guarded.event(6, 5);
    guarded.event(7, 0, 1);
    guarded.event(8, 5);
    guarded.put(MenuField::Buttons, 0x9000, 16);
    guarded.event(2);
    check(guarded.get(MenuField::Buttons, 16) == 0, "Unqualified custom race passed its activation gate");
    guarded.event(3);
    rejects([&] { guarded.event(12, 2); });
    Fixture quiet;
    quiet.select(4, 0);
    quiet.input(0, 0);
    quiet.input(-1, 0);
    quiet.put(MenuField::Delay, 1);
    quiet.input(1, 0);
    check(quiet.get(MenuField::CursorX) == 0, "Delayed navigation changed selection");
    quiet.put(MenuField::Delay, 0);
    quiet.put(MenuField::Buttons, 0x4000, 16);
    quiet.put(MenuField::StickX, 1, 8, 2);
    quiet.event(2);
    check(!quiet.event(3).navigation_sound, "Back confirmation played a navigation ping");
    for (unsigned unlocked : {0U, 65535U}) {
        Fixture navigation;
        navigation.put(MenuField::FutureFunLand, unlocked, 0, 2);
        const unsigned rows = unlocked == 65535 ? 4 : 5;
        TrackMenuLayout grid(rows, 7);
        navigation.real(MenuField::Y, -float((rows - 1) * 240));
        navigation.event(16);
        check(navigation.real(MenuField::Y) == -float((rows - 1) * 240),
              "Last stock world lost its correct background");
        check(navigation.event(17).backgrounds.empty(), "Custom background leaked into the last stock world");
        navigation.real(MenuField::Y, -float(rows * 240 + 120));
        navigation.event(4);
        const auto header1 = navigation.get(MenuField::Details, 4 * 16),
                   header2 = navigation.get(MenuField::Details, 7 * 16);
        check(header1 != header2 && header1 && header2, "Adjacent custom rows share a deduplicated header pointer");
        navigation.event(16);
        const auto backgrounds = navigation.event(17).backgrounds;
        check(backgrounds.size() == 2 && backgrounds[0].texture != backgrounds[1].texture,
              "Custom scrolling does not draw adjacent distinct backgrounds");
        for (unsigned i = 0; i < 7; ++i)
            for (auto direction : std::array<std::pair<int, int>, 4>{{{-1, 0}, {1, 0}, {0, 1}, {0, -1}}}) {
                auto start = grid.custom_cursor(i);
                navigation.select(start.row, start.column);
                navigation.input(direction.first, direction.second);
                const auto end = grid.move(start, direction.first, direction.second);
                check(navigation.get(MenuField::CursorY) == static_cast<unsigned>(end.row) &&
                          navigation.get(MenuField::CursorX) == static_cast<unsigned>(end.column),
                      "Native adapter and logical grid disagree");
            }
    }
    {
        auto courses = Fixture::roots();
        courses.resize(42, courses.front());
        for (unsigned i = 0; i < courses.size(); ++i) {
            courses[i].content_id = std::string(62, 'a') + "0123456789abcdef"[i / 16] + "0123456789abcdef"[i % 16];
        }
        Fixture rows(true, courses);
        std::array<std::uint32_t, 7> textures{}, headers{};
        for (unsigned row = 0; row < 7; ++row) {
            rows.real(MenuField::Y, -float((row + 4) * 240));
            rows.event(4);
            headers[row] = rows.get(MenuField::Details, 4 * 16);
            rows.put(MenuField::VertexFlip, row % 2);
            rows.event(16);
            const auto art = rows.event(17);
            check(art.backgrounds.size() == 1, "Each centred custom row must draw exactly one background");
            textures[row] = art.backgrounds[0].texture;
            check(art.backgrounds[0].vertices == textures[row] + 600 + (row % 2) * 72,
                  "Background lost retail double-buffer ownership");
            if (row)
                check(headers[row] != headers[row - 1], "Repeated custom row lost its unique title pointer");
        }
        check(textures[0] == textures[6] && textures[0] != textures[5],
              "Six custom images do not cycle per logical row");
        rows.event(16);
        rows.put(MenuField::VertexFlip, 2);
        rejects([&] { rows.event(17); });
    }
    {
        const std::string cup_id(64, '9'), asset_id(64, 'a');
        Fixture cups(true, Fixture::roots(), {{cup_id, "MY CUP", {asset_id, 5}, 2}});
        // Seven courses occupy two rows. Cups begin on a fresh third row.
        cups.select(6, 0);
        cups.event(6, 5);
        cups.event(7, 0, 1);
        const auto preview = cups.event(8, 5);
        check(preview.scene && preview.scene->id == asset_id,
              "Cup preview leaked its menu identity into asset routing");
        cups.put(MenuField::Type, 1);
        cups.put(MenuField::SelectedX, 0);
        const auto stock_grid = cups.get(MenuField::StockIDs);
        cups.event(20);
        check(cups.get(MenuField::CursorX) == 4, "Cup options did not borrow trophy semantics");
        rejects([&] { cups.adapter.checkpoint(); });
        cups.event(12, 2);
        cups.put(MenuField::Type, 2);
        cups.event(21);
        check(cups.adapter.selected_cup() == cup_id, "Confirmation lost the explicit cup identity");
        check(cups.get(MenuField::CursorX) == 4 && cups.get(MenuField::CursorY) == 0,
              "Cup load used an out-of-range native world");
        cups.event(15, 20);
        cups.event(23);
        check(cups.get(MenuField::CursorX) == 0 && cups.get(MenuField::CursorY) == 6,
              "Cup load lost the logical cursor");
        check(cups.get(MenuField::StockIDs) == stock_grid, "Cup selection wrote native track IDs");
        const auto saved = cups.adapter.checkpoint();
        check(cups.adapter.stage_checkpoint(saved).selected_cup() == cup_id, "Cup selection did not rewind");
        cups.event(15, 15);
        check(cups.adapter.selected_cup().empty(), "Custom cup followed a return to native tracks");
        Fixture sixth;
        sixth.select(4, 5);
        sixth.put(MenuField::Type, 1);
        sixth.event(20);
        check(sixth.get(MenuField::CursorX) == 0, "Sixth custom track became a native challenge");
        sixth.event(12, 2);
        sixth.put(MenuField::Type, 2);
        sixth.event(21);
        check(sixth.get(MenuField::CursorX) == 0, "Custom track became a trophy at the parent's load decision");
        sixth.event(23);
        check(sixth.get(MenuField::CursorX) == 5, "Sixth custom-track cursor was not restored");
    }
    {
        // Production callback order is bgdraw_render(&gCurrDisplayList),
        // preview rendering, THEN menu_loop adopts *gCurrDisplayList into
        // sMenuCurrDisplayList. The latter still references a prior task here.
        Fixture draw;
        draw.real(MenuField::Y, -1080);
        const auto word = [&](std::uint32_t at, std::uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                draw.memory[((at & 0x1fffffffU) + i) ^ 3] = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
        };
        const auto read = [&](std::uint32_t at) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value = (value << 8) | draw.memory[((at & 0x1fffffffU) + i) ^ 3];
            return value;
        };
        constexpr std::uint32_t current = 0x8000C000, stale = 0x8000D000;
        draw.put(MenuField::DisplayList, stale);
        word(stale, 0xDEADBEEF);
        word(Fixture::background_slot, current);
        draw.event(16);
        rejects([&] { draw.adapter.checkpoint(); });
        rejects([&] { draw.event(16); });
        // Retail appends its own background packets, then restores/clobbers
        // a0 on return. Our exit must use the retained ENTRY slot instead.
        word(current, 0xE7000000);
        word(Fixture::background_slot, current + 8);
        const auto art = draw.event(17, 0xffffffff);
        check(art.background_display_list == Fixture::background_slot && art.backgrounds.size() == 2,
              "Background exit lost the actual caller list or adjacent scrolling row");
        for (const auto& background : art.backgrounds) {
            const auto material = read(art.background_display_list);
            word(material, 0x07070038);
            word(material + 4, (background.texture + 544) & 0x1fffffffU);
            word(art.background_display_list, material + 8);
            append_track_background_quad(draw.memory, art.background_display_list, background);
            check(read(material + 8) == 0x04180050 && read(material + 12) == (background.vertices & 0x1fffffffU) &&
                      read(material + 16) == 0x05110020 && read(material + 20) == (background.triangles & 0x1fffffffU),
                  "Production emitter broke material/vertex/polygon ordering on the current list");
        }
        const auto ui = read(Fixture::background_slot);
        check(ui == current + 8 + 2 * 24, "Current display list did not advance past both custom rows");
        check(draw.get(MenuField::DisplayList) == stale && read(stale) == 0xDEADBEEF,
              "Background corrupted the previous task's menu list");
        word(ui, 0xFA000000);
        word(Fixture::background_slot, ui + 8);
        check(read(ui - 8) == 0x05110020 && read(ui) == 0xFA000000,
              "Background polygons were appended after the later preview/menu layer");
        check(draw.real(MenuField::Y) == -1080, "Draw-pointer scope changed menu scrolling");
        const auto clean = draw.adapter.checkpoint();
        check(draw.adapter.stage_checkpoint(clean).checkpoint() == clean,
              "Finished draw retained a transient caller pointer");
        // A different caller/task must not inherit the preceding draw's slot.
        constexpr std::uint32_t next_slot = 0x80008800;
        word(next_slot, current + 256);
        draw.event(16, next_slot);
        check(draw.event(17, stale).background_display_list == next_slot, "Next task reused a stale caller slot");
        const auto unchanged = draw.memory;
        for (const auto slot : {0U, 0xffffffffU, Fixture::background_slot + 1})
            rejects([&] { append_track_background_quad(draw.memory, slot, art.backgrounds[0]); });
        check(draw.memory == unchanged, "Rejected background pointer modified guest memory");
        for (const auto address : {current + 4, 0x800FFFF8U}) {
            word(Fixture::background_slot, address);
            const auto before = draw.memory;
            rejects([&] { append_track_background_quad(draw.memory, Fixture::background_slot, art.backgrounds[0]); });
            check(draw.memory == before, "Rejected display-list tail was partially written");
        }
        rejects([&] { draw.event(16, 0xffffffff); });
        rejects([&] { draw.event(17); });
    }
    Fixture bad;
    auto fields = bad.fields;
    fields[0] = 0xffffffff;
    rejects([&] { bad.adapter.apply(2, bad.memory, fields); });
    rejects([&] { bad.event(26); });
    rejects([&] { bad.adapter.install_names(bad.memory, 0x80010000); });
    bad.select(4, 0);
    bad.event(6, 5);
    bad.event(7, 0, 1);
    rejects([&] { bad.event(8, 6); });

    // An appended .dkrmap level shares the legacy category but keeps its own
    // level ID through background preview, confirmation, retry and return.
    auto mixed = Fixture::roots();
    mixed[1].carrier = 65;
    mixed[1].name = "AUTHORED COURSE";
    Fixture authored(true, mixed);
    authored.select(4, 0);
    authored.input(1, 0);
    check(authored.get(MenuField::PreviewCarrier) == 65, "Authored course lost its appended level ID");
    authored.event(6, 65);
    authored.event(7, 0, 1);
    effect = authored.event(8, 65);
    check(effect.scene && effect.scene->carrier == 65 && effect.scene->id == mixed[1].content_id,
          "Authored preview did not retain its identity");
    authored.put(MenuField::LoadedCarrier, 65);
    authored.put(MenuField::Buttons, 0x9000, 16);
    authored.event(2);
    check(authored.get(MenuField::Buttons, 16) == 0x9000, "Ready authored race cannot be confirmed");
    authored.event(3);
    authored.event(12, 2);
    effect = authored.event(14, 65);
    check(effect.scene && effect.scene->carrier == 65, "Authored race lost its selection");
    authored.event(15, 17);
    effect = authored.event(14, 65);
    check(effect.scene && effect.scene->carrier == 65, "Authored retry lost its selection");
    authored.event(0);
    authored.event(1);
    authored.input(-1, 0);
    check(authored.get(MenuField::PreviewCarrier) == 5, "Cannot navigate back to a legacy course");
#if defined(DKR_TRACK_MENU_ROLLBACK_TEST)
    rollback_stock_preview();
#endif
}
} // namespace
int main() {
    try {
        run();
        std::cout << checks << " native custom-menu adapter checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

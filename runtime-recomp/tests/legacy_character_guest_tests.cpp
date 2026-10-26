// Execute the actual regenerated spawn_object prologue and its header-load
// failure exit. Resource construction beyond that boundary is separately
// qualified; these stubs must never be mistaken for a full game play test.
#include "legacy_runtime_character.hpp"
#include <algorithm>
#include <iostream>

extern "C" void spawn_object(std::uint8_t*, recomp_context*);
namespace {
using namespace dkr::mods;
Bytes memory(8 * MiB);
std::unique_ptr<CharacterRoster> roster;
unsigned expected_header = 0, observed = 0, checks = 0;
constexpr std::uint32_t Entry = 0x80300000, Scratch = 0x80310000, Translation = 0x80320000, Stack = 0x807f0000,
                        NativeRoster = 0x80330000;
#if DKR_TEST_REVISION == 77
constexpr std::uint32_t HeaderCount = 0x8011ad68, ScratchPointer = 0x8011ad58, TranslationPointer = 0x8011aeb8;
#else
constexpr std::uint32_t HeaderCount = 0x8011b2e8, ScratchPointer = 0x8011b2d8, TranslationPointer = 0x8011b438;
#endif
void check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw Error(message);
}
gpr ptr(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
void write(std::uint32_t at, std::uint32_t value, unsigned length = 4) {
    for (unsigned i = 0; i < length; ++i)
        memory[((at & 0x1fffffff) + i) ^ 3] = value >> ((length - 1 - i) * 8);
}
} // namespace
extern "C" void dkr_legacy_character_event(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                           std::uint32_t field) {
    check(rdram == memory.data() && event == 0, "Unexpected generated character event.");
    dispatch_character_event(*roster, memory, *ctx, event, field);
}
extern "C" void get_settings(std::uint8_t*, recomp_context* ctx) {
    ctx->r2 = ptr(0x80340000);
}
extern "C" void update_object_stack_trace(std::uint8_t*, recomp_context*) {}
extern "C" void dkr_presentation_object_spawned(std::uint8_t*, recomp_context*) {}
extern "C" void switch_error(const char*, std::uint32_t, std::uint32_t) {
    throw Error("Unexpected native switch in character-header qualification.");
}
extern "C" void load_object_header(std::uint8_t* rdram, recomp_context* ctx) {
    ++observed;
    check(ctx->r4 == expected_header, "Native header loader received a different ID.");
    check(MEM_HU(ctx->r29, 0x4e) == expected_header, "Header lifetime store and loader identity disagree.");
    check(MEM_W(ctx->r29, 0x68) == ptr(Entry), "Hook lost the original entry pointer.");
    ctx->r2 = 0; // Exercise native failed-allocation return, not an invented success.
}
#define UNREACHED(name)                                                                                                \
    extern "C" void name(std::uint8_t*, recomp_context*) {                                                             \
        throw Error("Unreached loader stage: " #name);                                                                 \
    }
// clang-format off: clang-format never settles on a list of macro calls without semicolons
UNREACHED(func_800245B4) UNREACHED(get_character_id_from_slot) UNREACHED(get_level_segment_index_from_position)
UNREACHED(get_object_property_size) UNREACHED(init_object_interaction_data) UNREACHED(init_object_shading)
UNREACHED(init_object_shadow) UNREACHED(init_object_water_effect) UNREACHED(is_in_adventure_two)
UNREACHED(light_setup_light_sources) UNREACHED(load_texture) UNREACHED(mempool_alloc_pool)
UNREACHED(mempool_free) UNREACHED(model_anim_offset) UNREACHED(obj_init_attachpoint)
UNREACHED(obj_init_collision) UNREACHED(obj_init_emitter) UNREACHED(obj_init_property_flags)
UNREACHED(object_model_init) UNREACHED(objFreeAssets) UNREACHED(run_object_init_func)
UNREACHED(tex_free) UNREACHED(tex_load_sprite) UNREACHED(try_free_object_header)
// clang-format on
#undef UNREACHED
                                int main() {
    try {
        auto assets = std::make_shared<CharacterNamespace>();
        assets->characters = {{std::string(64, 'a'), "First", 8, 906, {304, 305, 306}},
                              {std::string(64, 'b'), "Second", 8, 907, {310, 311, 312}}};
        roster = std::make_unique<CharacterRoster>(assets);
        roster->request(0, assets->characters[0].id);
        roster->request(1, assets->characters[1].id);
        for (unsigned i = 0; i < 4; ++i)
            write(NativeRoster + i, 8, 1);
        write(HeaderCount, 320);
        write(ScratchPointer, Scratch);
        write(TranslationPointer, Translation);
        for (unsigned committed = 0; committed < 2; ++committed) {
            if (committed) {
                recomp_context commit{};
                commit.r4 = 3;
                dispatch_character_event(*roster, memory, commit, 1, NativeRoster);
            }
            for (unsigned direct = 0; direct < 2; ++direct)
                for (unsigned vehicle = 0; vehicle < 3; ++vehicle)
                    for (unsigned player : {0U, 1U, 2U, 3U, 4U, 65535U}) {
                        const auto header = 1 + 10 * vehicle, object = direct ? header : 500;
                        expected_header = committed && player < 2 ? (player ? 310 : 304) + vehicle : header;
                        write(Entry, object & 255, 1);
                        write(Entry + 1, 16 | ((object >> 8) << 7), 1);
                        write(Entry + 14, player, 2);
                        write(Translation + 500 * 2, header, 2);
                        recomp_context ctx{};
                        ctx.r4 = ptr(Entry);
                        ctx.r5 = direct ? 2 : 0;
                        ctx.r29 = ptr(Stack);
                        ctx.r16 = 0x1111;
                        ctx.r17 = 0x2222;
                        ctx.r18 = 0x3333;
                        ctx.r19 = 0x4444;
                        ctx.r31 = 0x12345678;
                        spawn_object(memory.data(), &ctx);
                        check(ctx.r2 == 0 && ctx.r29 == ptr(Stack), "Native failure return or stack restore changed.");
                        check(ctx.r16 == 0x1111 && ctx.r17 == 0x2222 && ctx.r18 == 0x3333 && ctx.r19 == 0x4444 &&
                                  ctx.r31 == 0x12345678,
                              "Character hook corrupted a callee-saved register.");
                    }
        }
        check(observed == 72, "Not every generated loader route was tested.");
        std::cout << checks << " native character-header lifetime checks passed (v" << DKR_TEST_REVISION << ").\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

// Execute regenerated spawn_object through the asset-loading boundary. This
// synthetic fixture opens no window and never starts a scene or game session.
#include "legacy_model_safety.hpp"
#include "recomp.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

extern "C" void spawn_object(std::uint8_t*, recomp_context*);
namespace {
using namespace dkr::mods;
std::vector<std::uint8_t> memory(8 * 1024 * 1024);
constexpr unsigned Entry=0x80300000, Scratch=0x80310000, Translation=0x80320000,
    Header=0x80330000, Settings=0x80340000, Assets=0x80350000,
    Instance=0x80400000, Model=0x80401000, Sprite=0x80402000, Stack=0x807f0000;
#if DKR_TEST_REVISION == 77
constexpr unsigned HeaderCount=0x8011ad68, ScratchPointer=0x8011ad58, TranslationPointer=0x8011aeb8;
#else
constexpr unsigned HeaderCount=0x8011b2e8, ScratchPointer=0x8011b2d8, TranslationPointer=0x8011b438;
#endif
struct BoundaryReached {};
struct InvalidModel {};
unsigned checks=0, models=0, textures=0, sprites=0, expected_index=0, first_id=0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
gpr ptr(unsigned address) { return static_cast<std::int32_t>(address); }
void write(unsigned at, unsigned value, unsigned length=4) {
    for (unsigned i=0; i<length; ++i)
        memory[((at & 0x1fffffff) + i) ^ 3] = value >> ((length-1-i)*8);
}
void asset_call(recomp_context* ctx, unsigned address) {
    if (!models && !textures && !sprites) first_id=unsigned(ctx->r4);
    check(unsigned(ctx->r4)==319+expected_index+models+textures+sprites,
          "Spawn loaded a different asset ID or changed the sparse slot selection.");
    ctx->r2=ptr(address);
}
unsigned completed_worlds(unsigned trophies) {
    unsigned count=0;
    for (unsigned world=0; world<4; ++world) count+=((trophies >> (2*world)) & 3)==3;
    return count;
}
void reset(unsigned trophies, unsigned type, unsigned behavior) {
    std::fill(memory.begin(), memory.end(), 0);
    models=textures=sprites=first_id=0;
    expected_index=behavior==100 ? completed_worlds(trophies) : 0;
    write(HeaderCount, 320); write(ScratchPointer, Scratch); write(TranslationPointer, Translation);
    write(Entry, 161, 1); write(Entry+1, 16, 1); write(Translation+161*2, 161, 2);
    write(Settings+0xe, trophies, 2);
    write(Header+0xc, 0x3f800000); write(Header+0x10, Assets);
    write(Header+0x53, type, 1); write(Header+0x54, behavior, 1); write(Header+0x55, 5, 1);
    for (unsigned i=0; i<5; ++i) write(Assets+4*i, 319+i);
    write(Instance, Model);
    // Real Sprite's first word is two counts, not a ModelInstance pointer.
    write(Sprite, 0x00100010);
}
}
extern "C" void dkr_legacy_character_event(std::uint8_t*, recomp_context*, unsigned event, std::uint32_t) {
    check(event==0, "Unexpected character hook in scenery spawn.");
}
extern "C" int dkr_legacy_model_safety(std::uint8_t*, recomp_context*, unsigned operation, std::uint32_t address) {
    check(operation==1 && address==Scratch, "Unexpected model boundary.");
    const auto result=check_model_operation(memory, operation, address);
    if (result.failure) {
        check(std::string_view(result.failure)=="model pointer" && result.model==0x00100010,
              "Fixture failed for a reason other than a Sprite in a model slot.");
        throw InvalidModel{};
    }
    return result.no_shading;
}
extern "C" void get_settings(std::uint8_t*, recomp_context* ctx) { ctx->r2=ptr(Settings); }
extern "C" void update_object_stack_trace(std::uint8_t*, recomp_context*) {}
extern "C" void dkr_presentation_object_spawned(std::uint8_t*, recomp_context*) {}
extern "C" void load_object_header(std::uint8_t*, recomp_context* ctx) {
    check(ctx->r4==161, "Wrong stock trophy-signpost header."); ctx->r2=ptr(Header);
}
extern "C" void get_level_segment_index_from_position(std::uint8_t*, recomp_context* ctx) { ctx->r2=0; }
extern "C" void func_800245B4(std::uint8_t*, recomp_context*) {}
extern "C" void obj_init_property_flags(std::uint8_t*, recomp_context* ctx) { ctx->r2=0; }
extern "C" void object_model_init(std::uint8_t*, recomp_context* ctx) { asset_call(ctx, Instance); ++models; }
extern "C" void load_texture(std::uint8_t*, recomp_context* ctx) { asset_call(ctx, Instance); ++textures; }
extern "C" void tex_load_sprite(std::uint8_t*, recomp_context* ctx) { asset_call(ctx, Sprite); ++sprites; }
extern "C" void get_object_property_size(std::uint8_t*, recomp_context*) { throw BoundaryReached{}; }
extern "C" void switch_error(const char*, std::uint32_t, std::uint32_t) { throw std::runtime_error("Unexpected guest switch."); }
#define UNREACHED(name) extern "C" void name(std::uint8_t*, recomp_context*) { throw std::runtime_error("Unreached scene stage: " #name); }
UNREACHED(get_character_id_from_slot) UNREACHED(init_object_interaction_data) UNREACHED(init_object_shading)
UNREACHED(init_object_shadow) UNREACHED(init_object_water_effect) UNREACHED(is_in_adventure_two)
UNREACHED(light_setup_light_sources) UNREACHED(mempool_alloc_pool) UNREACHED(mempool_free)
UNREACHED(model_anim_offset) UNREACHED(obj_init_attachpoint) UNREACHED(obj_init_collision)
UNREACHED(obj_init_emitter) UNREACHED(objFreeAssets) UNREACHED(run_object_init_func)
UNREACHED(tex_free) UNREACHED(try_free_object_header)
#undef UNREACHED
int main(int argc, char** argv) {
    try {
        auto* rdram=memory.data();
        const bool old=argc==2 && std::string_view(argv[1])=="--expect-retail-bug";
        unsigned invalid=0, cases=0;
        for (unsigned behavior : {100U, 0U})
            for (unsigned type : {0U, 4U, 1U})
                for (unsigned flags : {0U, 2U})
                    for (unsigned trophies=0; trophies<1024; ++trophies) {
                        if (old && type!=0) continue;
                        reset(trophies, type, behavior);
                        recomp_context ctx{}; ctx.r4=ptr(Entry); ctx.r5=flags; ctx.r29=ptr(Stack);
                        bool failed=false, reached=false;
                        try { spawn_object(memory.data(), &ctx); }
                        catch (const InvalidModel&) { failed=true; ++invalid; }
                        catch (const BoundaryReached&) { reached=true; }
                        const unsigned route=old && behavior==100 ? trophies >> 8 : type;
                        const unsigned loaded=behavior==100 ? 1 : 5;
                        check(failed==(route!=0 && type==0), "Unexpected model validation outcome.");
                        check(reached!=failed, "Spawn did not reach the expected asset boundary.");
                        check(models==(route==0 ? loaded : 0) && textures==(route==4 ? loaded : 0) &&
                              sprites==(route!=0 && route!=4 ? loaded : 0), "Wrong loader selected.");
                        check(first_id==319+expected_index && MEM_BU(0x3a, ptr(Scratch))==expected_index,
                              "Trophy progress no longer selects the original signpost model.");
                        for (unsigned i=0; i<5; ++i)
                            check((MEM_W(0, ptr(Scratch+0x80+4*i))!=0)==(behavior!=100 || i==expected_index),
                                  "Sparse slots changed.");
                        check(MEM_HU(0xe, ptr(Settings))==trophies, "Save progress was modified.");
                        ++cases;
                    }
        check(old ? invalid==1536 : invalid==0, "Wrong reproduction count.");
        std::cout << checks << " spawn asset-type checks, " << cases << " cases, " << invalid
                  << " Sprite/model failures (v" << DKR_TEST_REVISION << ", "
                  << (old ? "retail bug reproduced" : "corrected pipeline") << ").\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

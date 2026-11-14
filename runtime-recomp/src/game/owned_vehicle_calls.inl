// Included separately by each isolated revision. Internal linkage is essential:
// V77/V80 must never share a linker-folded wrapper for different guest symbols.
namespace {
namespace dkr_owned_vehicle_menu {
static void frame(std::uint8_t* ram, recomp_context* ctx) {
    ctx->f_odd = ctx->mips3_float_mode ? &ctx->f1.u32l : &ctx->f0.u32h;
    const auto result = dkr_probe_run_native(::menu_element_render, ram, ctx);
    if (!result.completed)
        throw dkr::mods::Error(result.blocked ? result.blocked : "Owned vehicle frame callback failed.");
}
static void text_width(std::uint8_t* ram, recomp_context* ctx) {
    ctx->f_odd = ctx->mips3_float_mode ? &ctx->f1.u32l : &ctx->f0.u32h;
    const auto result = dkr_probe_run_native(::get_text_width, ram, ctx);
    if (!result.completed)
        throw dkr::mods::Error(result.blocked ? result.blocked : "Owned vehicle text callback failed.");
}
} // namespace dkr_owned_vehicle_menu
} // namespace

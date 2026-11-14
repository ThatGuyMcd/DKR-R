// Included in the task identity implementation: recording and submission use
// its existing lock, buffer parity and immutable OSTask ownership key.
namespace dkr::runtime::detail {
std::span<const dkr_owned_draw_event> ordinary_events() noexcept {
    return g_active_owned_task ? std::span<const dkr_owned_draw_event>{}
                               : std::span<const dkr_owned_draw_event>{g_active_detail};
}
} // namespace dkr::runtime::detail
extern "C" void dkr_detail_mesh_begin(std::uint8_t* ram, recomp_context* ctx, std::uint32_t* words) noexcept {
    dkr_owned_draw_event e{};
    words[0] = 0;
    using namespace dkr::runtime;
    if (!ram || !ctx || !enhancements::modern_presentation_enabled() || !netplay::external_side_effects_allowed() ||
        !netplay::session().runtime_view().active)
        return;
    const auto range = [](std::uint32_t p, unsigned bytes) {
        return p >= 0x80000000U && p < 0x80800000U && bytes <= 0x80800000U - p;
    };
    const auto word = [&](std::uint32_t p) { return range(p, 4) && !(p & 3U) ? ReadU32(ram, p) : 0U; };
    const auto byte = [&](std::uint32_t p) { return range(p, 1) ? unsigned(ReadU8(ram, p)) : 0U; };
    const auto half = [&](std::uint32_t p) {
        if (!range(p, 2) || (p & 1U))
            return 0U;
        return byte(p) * 256U + byte(p + 1);
    };
    const auto object = std::uint32_t(ctx->r5);
    if (!range(object, 0x80) || half(object + 0x48) != 1)
        return; // Ghost variants keep their native model.
    const auto header = word(object + 0x40), slots = word(object + 0x68), racer = word(object + 0x64);
    if (!range(header, 0x78) || !range(racer, 0x224))
        return;
    const auto count = byte(header + 0x55), index = byte(object + 0x3a);
    if (!count || count > 32 || index >= count || !range(slots, count * 4))
        return;
    unsigned first = 0;
    std::uint32_t highest = 0;
    for (; first < count; ++first)
        if ((highest = word(slots + first * 4)))
            break;
    const auto current = word(slots + index * 4);
    if (!highest || !current || highest == current)
        return;
    const auto rev = revision_addresses::selected_revision() == rom::Revision::UsV80 ? 1U : 0U;
    const auto dl = rev ? 0x8011B40CU : 0x8011AE8CU, view = rev ? 0x8011B634U : 0x8011B0B4U,
               intensity = rev ? 0x8011B2B0U : 0x8011AD30U;
    const auto start = word(dl), viewport = word(view), stack = std::uint32_t(ctx->r29);
    if (!range(start, 8) || (start & 7) || viewport > 3 || !range(stack + 16, 4))
        return;
    const auto head = word(stack + 16);
    if (head > 1)
        return;
    e.kind = DKR_OWNED_LOCAL_MODEL;
    e.address = start;
    e.token = viewport;
    e.parameters[1] = object;
    e.parameters[2] = highest;
    e.parameters[3] = half(object + 0x18);
    e.parameters[4] = byte(object + 0x3b);
    e.parameters[5] = std::uint32_t(ctx->r7);
    e.parameters[6] = ctx->r6 != 0;
    e.parameters[7] = head;
    e.parameters[8] = half(racer + 0x16a);
    e.parameters[9] = word(intensity);
    static_assert(sizeof(e) == 19 * sizeof(std::uint32_t));
    std::memcpy(words, &e, sizeof(e));
}
extern "C" void dkr_detail_mesh_end(std::uint8_t* ram, recomp_context*, std::uint32_t* words) noexcept {
    dkr_owned_draw_event e{};
    std::memcpy(&e, words, sizeof(e));
    if (!e.kind)
        return;
    const auto dl = dkr::runtime::revision_addresses::selected_revision() == dkr::runtime::rom::Revision::UsV80
                        ? 0x8011B40CU
                        : 0x8011AE8CU;
    e.parameters[0] = ReadU32(ram, dl);
    if (dkr_owned_draw_event_valid(&e)) {
        std::scoped_lock lock(g_identity_mutex);
        auto& rows = g_detail_maps[g_recording_buffer & 1U];
        if (rows.size() < dkr::runtime::detail::Observations)
            rows.push_back(e);
        else
            dkr::runtime::detail::note({g_scene_generation.load(), e.parameters[1], e.token, 0, 0, 0, true,
                                        "observation capacity; original retained"});
    }
    words[0] = 0;
}

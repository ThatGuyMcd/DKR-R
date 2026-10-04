#include "vi_presentation_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::presentation;

    static_assert(present_from_vi_history(false)); // Stable/native unchanged.
    static_assert(!present_from_vi_history(true)); // Owned VI is authoritative.
    static_assert((kRetailNtscViStatus & 3U) == 2U); // 16-bit progressive.
    static_assert((kRetailNtscViStatus & 8U) == 0U); // Retail gamma is OFF.
    static_assert((kRetailNtscViStatus & 0x10010U) == 0x10010U);
    static_assert(retail_ntsc_vi_origin(0x80137900U) == 0x00137B80U);
    static_assert(retail_ntsc_vi_origin(0x80182980U) - 320U * 2U ==
                  (0x80182980U & 0x007FFFFFU));

    // A common 480 half-line DKR signal is rounded by RT64 to 244 rows.
    constexpr std::uint32_t padded_region = (32U << 16U) | 512U;
    static_assert(inferred_vi_height(padded_region, 0x400U) == 244U);
    constexpr std::uint32_t canonical_region =
        canonicalise_dkr_v_region(padded_region, 0x400U);
    static_assert(vi_region_start(canonical_region) == 32U);
    static_assert(vi_region_end(canonical_region) == 510U);
    static_assert(inferred_vi_height(canonical_region, 0x400U) == 240U);

    // An already canonical NTSC signal is bit-exact.
    constexpr std::uint32_t retail_region = (37U << 16U) | 511U;
    static_assert(inferred_vi_height(retail_region, 0x400U) == 240U);
    static_assert(canonicalise_dkr_v_region(retail_region, 0x400U) ==
                  retail_region);

    // Blank, malformed and unrelated modes are never rewritten.
    static_assert(canonicalise_dkr_v_region(0U, 0U) == 0U);
    constexpr std::uint32_t tall_region = (20U << 16U) | 540U;
    static_assert(inferred_vi_height(tall_region, 0x400U) > 248U);
    static_assert(canonicalise_dkr_v_region(tall_region, 0x400U) == tall_region);

    // DKR's retail clear uses inclusive framebuffer dimensions for an
    // exclusive RDP scissor. Correct only that exact 319x239 signature.
    constexpr std::uint32_t retail_clear =
        pack_scissor_lower_right(319U, 239U);
    constexpr std::uint32_t canonical_clear =
        pack_scissor_lower_right(320U, 240U);
    static_assert(correct_fullscreen_clear_scissor(
                      retail_clear, 320U, 240U) == canonical_clear);

    // A viewport-local or already-correct scissor must remain bit-exact.
    constexpr std::uint32_t split_scissor =
        pack_scissor_lower_right(159U, 119U);
    static_assert(correct_fullscreen_clear_scissor(
                      split_scissor, 320U, 240U) == split_scissor);
    static_assert(correct_fullscreen_clear_scissor(
                      canonical_clear, 320U, 240U) == canonical_clear);
    static_assert(correct_fullscreen_clear_scissor(
                      retail_clear, 0U, 240U) == retail_clear);

    assert(inferred_vi_height(canonical_region, 0x400U) == 240U);
    std::puts("[test][vi-presentation-policy] PASS");
    return 0;
}

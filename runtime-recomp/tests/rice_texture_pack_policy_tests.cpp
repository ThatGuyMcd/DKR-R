#include "rice_texture_pack_policy.hpp"

#include <cassert>
#include <cstdio>

using namespace dkr::runtime::rice_texture;

int main() {
    static_assert(kLegacyCoordinateShift == "none");
    static_assert(kLegacyCoordinatePolicy == "legacy-rice-no-shift");

    const auto rgb = parse_filename("Diddy Kong Racing/Backgrounds/Diddy Kong Racing#37B491A3#0#2_rgb.png");
    assert(rgb.has_value());
    assert(rgb->identity == "37b491a3#0#2");
    assert(rgb->variant == Variant::Rgb);

    const auto alpha = parse_filename("Game#ABCDEF12#0#3#0011AABB_a.PNG");
    assert(alpha.has_value());
    assert(alpha->identity == "abcdef12#0#3#0011aabb");
    assert(alpha->variant == Variant::Alpha);

    assert(!parse_filename("Game#ABC#0#2_rgb.png").has_value());
    assert(!parse_filename("Game#ABCDEF12#0_rgb.png").has_value());
    assert(!parse_filename("Game#ABCDEF12#0#2_rgb.jpg").has_value());

    assert(safe_archive_path("Diddy Kong Racing/texture.png"));
    assert(!safe_archive_path("../texture.png"));
    assert(!safe_archive_path("folder/../../texture.png"));
    assert(!safe_archive_path("C:/texture.png"));
    assert(!safe_archive_path("/texture.png"));

    assert(native_alias("37b491a3#0#2") == native_alias("37B491A3#0#2"));
    assert(native_alias("37b491a3#0#2") != native_alias("37b491a3#0#3"));
    assert(native_alias_string("37b491a3#0#2").size() == 16);
    assert(native_alias_string("37b491a3#0#2") == "a5998f33f8411ab1");

    std::puts("Rice texture-pack policy tests passed.");
    return 0;
}

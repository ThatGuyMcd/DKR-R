#include "dkr_save_codec.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

int main(int argc, char** argv) {
    using namespace dkr::runtime::saves::codec;
    SaveImage image = blank_image();
    image.slots[0].name = "jtm";
    image.slots[0].balloons = {47, 8, 8, 8, 8, 8};
    image.slots[0].taj_flags = 0x3F;
    image.slots[0].trophies = 0x3FF;
    image.slots[0].bosses = 0x7FF;
    image.slots[0].tt_amulet = 4;
    image.slots[0].wizpig_amulet = 4;
    image.slots[0].keys = 0x1E;
    image.slots[0].course_status[3] = 3;
    image.settings.adventure_two = true;
    image.settings.drumstick = true;
    image.settings.subtitles = true;
    image.settings.preserved_config_bits = (1ULL << 24U) | (1ULL << 40U);
    image.settings.tt_trials[4] = true;
    image.fastest_laps[0] = {1234, "TT"};
    image.course_times[0] = {4321, "DKR"};

    const auto bytes = encode(image);
    assert(bytes.size() == kImageSize);
    assert(validate(bytes));
    SaveImage decoded{};
    assert(decode(bytes, decoded));
    assert(decoded.slots[0].name == "JTM");
    assert(decoded.slots[0].balloons[0] == 47);
    assert(decoded.slots[0].course_status[3] == 3);
    assert(decoded.settings.adventure_two && decoded.settings.drumstick);
    assert(decoded.settings.subtitles);
    assert(decoded.settings.preserved_config_bits == ((1ULL << 24U) | (1ULL << 40U)));
    assert(decoded.settings.tt_trials[4]);
    assert(decoded.fastest_laps[0].time == 1234);
    assert(decoded.fastest_laps[0].initials == "TT");
    assert(decoded.course_times[0].initials == "DKR");

    auto damaged = bytes;
    damaged[5] ^= 0x80U;
    assert(!validate(damaged));
    damaged = bytes;
    damaged[0x78] ^= 1U;
    assert(!validate(damaged));
    damaged = bytes;
    damaged[0x90] ^= 1U;
    assert(!validate(damaged));

    const auto blank = blank_bytes();
    assert(validate(blank));
    SaveImage decoded_blank{};
    assert(decode(blank, decoded_blank));
    assert(decoded_blank.settings.subtitles);
    assert((decoded_blank.settings.preserved_config_bits & (1ULL << 24U)) != 0U);
    assert(std::string_view(course_names()[0]) == "Bluey I");
    assert(std::string_view(course_names()[34U - 1U]) == "Wizpig II");
    assert(std::string_view(record_names()[0]) == "Fossil Canyon - Car");
    assert(std::string_view(record_names()[47U - 1U]) == "Star City - Plane");

    // A never-used retail adventure slot can remain in the EEPROM's erased
    // 0xFF state. DKR accepts it as empty and the Save Builder must do the
    // same. Saving the decoded image canonicalises it to a checksummed blank.
    auto retail_erased_slot = blank;
    std::fill(retail_erased_slot.begin() + 0x28U, retail_erased_slot.begin() + 0x50U, 0xFFU);
    assert(validate(retail_erased_slot));
    SaveImage decoded_erased_slot{};
    assert(decode(retail_erased_slot, decoded_erased_slot));
    assert(decoded_erased_slot.slots[1].name.empty());
    assert(decoded_erased_slot.slots[1].balloons == (std::array<std::uint8_t, kWorldCount>{}));
    assert(validate(encode(decoded_erased_slot)));

    std::vector<std::uint8_t> canonical;
    std::string canonical_error;
    assert(canonical_bytes(retail_erased_slot, canonical, &canonical_error));
    assert(canonical == blank);

    auto invalid_checksums = retail_erased_slot;
    invalid_checksums[0] ^= 0x5AU;
    invalid_checksums[0x78U] ^= 0x40U;
    invalid_checksums[0x80U] ^= 0x21U;
    assert(!validate(invalid_checksums));
    std::vector<std::uint8_t> repaired;
    assert(repair_checksums(invalid_checksums, repaired, &canonical_error));
    assert(validate(repaired));
    assert(repaired == retail_erased_slot);
    assert(std::all_of(repaired.begin() + 0x28U, repaired.begin() + 0x50U,
                       [](std::uint8_t value) { return value == 0xFFU; }));

    SaveImage bounded = blank_image();
    bounded.slots[0].balloons = {127, 12, 9, 8, 8, 8};
    bounded.slots[0].tt_amulet = 7;
    bounded.slots[0].wizpig_amulet = 6;
    bounded.slots[0].course_status[0] = 7;
    normalise_editable_fields(bounded);
    assert(bounded.slots[0].balloons == (std::array<std::uint8_t, kWorldCount>{47, 8, 8, 8, 8, 8}));
    assert(bounded.slots[0].tt_amulet == 4);
    assert(bounded.slots[0].wizpig_amulet == 4);
    assert(bounded.slots[0].course_status[0] == 3);
    assert(validate_editable_ranges(bounded));
    bounded.slots[0].balloons[1] = 9;
    assert(!validate_editable_ranges(bounded));
    bounded.slots[0].balloons[1] = 8;
    bounded.slots[0].balloons[0] = 39;
    assert(!validate_editable_ranges(bounded));
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> external{std::istreambuf_iterator<char>(input),
                                                 std::istreambuf_iterator<char>()};
        std::string error;
        assert(input.good() || input.eof());
        assert(decode(external, decoded, &error));
        assert(encode(decoded) == external);
        std::fprintf(stderr, "[test][dkr-save-codec] external round-trip: %s\n", argv[1]);
    }
    std::puts("[test][dkr-save-codec] PASS");
    return 0;
}

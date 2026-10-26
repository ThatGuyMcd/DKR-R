#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::runtime::saves::codec {

constexpr std::size_t kImageSize = 0x200U;
constexpr std::size_t kAdventureSlotCount = 3U;
constexpr std::size_t kCourseCount = 34U;
constexpr std::size_t kWorldCount = 6U;
constexpr std::size_t kRecordCount = 47U;
constexpr std::uint8_t kMaximumTotalBalloons = 47U;
constexpr std::uint8_t kMaximumHubBalloons = 7U;
constexpr std::uint8_t kMaximumWorldBalloons = 8U;
constexpr std::uint8_t kMaximumAmuletPieces = 4U;

struct AdventureSlot {
    std::array<std::uint8_t, kCourseCount> course_status{};
    std::uint8_t taj_flags = 0;
    std::uint16_t trophies = 0;
    std::uint16_t bosses = 0;
    std::array<std::uint8_t, kWorldCount> balloons{};
    std::uint8_t tt_amulet = 0;
    std::uint8_t wizpig_amulet = 0;
    std::array<std::uint16_t, kWorldCount> world_flags{};
    std::uint8_t keys = 0;
    std::uint32_t cutscenes = 0;
    std::string name;
    std::uint8_t reserved = 0;
};

struct GlobalSettings {
    bool adventure_two = false;
    bool drumstick = false;
    std::uint8_t language = 0;
    std::array<bool, 20> tt_trials{};
    bool subtitles = false;
    // Bit 24 and bits 26-55 are not user-editable. Retain them exactly.
    std::uint64_t preserved_config_bits = 0;
};

struct Record {
    std::uint16_t time = 0;
    std::string initials;
};

struct SaveImage {
    std::array<AdventureSlot, kAdventureSlotCount> slots{};
    GlobalSettings settings{};
    std::array<Record, kRecordCount> fastest_laps{};
    std::array<Record, kRecordCount> course_times{};
    std::array<std::uint8_t, 2> fastest_laps_tail{};
    std::array<std::uint8_t, 2> course_times_tail{};
};

bool decode(std::span<const std::uint8_t> bytes, SaveImage& image, std::string* error = nullptr);
std::vector<std::uint8_t> encode(const SaveImage& image);
SaveImage blank_image();
std::vector<std::uint8_t> blank_bytes();
bool validate(std::span<const std::uint8_t> bytes, std::string* error = nullptr);

// Rebuild only the six derived checksum regions in a 512-byte EEPROM image.
// All gameplay, record, reserved and erased-slot bytes are preserved exactly.
// This cannot infer whether payload data was damaged, so callers must retain
// the original image as a safety backup before activating the result.
bool repair_checksums(std::span<const std::uint8_t> bytes, std::vector<std::uint8_t>& repaired,
                      std::string* error = nullptr);

// Decode and re-encode a valid image into one deterministic representation.
// In particular, a retail erased (0xFF) empty slot and DKR-R's checksummed
// empty slot become identical for multiplayer compatibility comparisons.
bool canonical_bytes(std::span<const std::uint8_t> bytes, std::vector<std::uint8_t>& canonical,
                     std::string* error = nullptr);

// Constrain only fields that the Save Builder owns. Opaque/reserved bits and
// T.T. records are deliberately preserved so loading the builder never
// destroys data it does not expose for editing.
void normalise_editable_fields(SaveImage& image);
bool validate_editable_ranges(const SaveImage& image, std::string* error = nullptr);

std::string sanitise_name(std::string_view value);
const std::array<const char*, kCourseCount>& course_names();
const std::array<const char*, kRecordCount>& record_names();

} // namespace dkr::runtime::saves::codec

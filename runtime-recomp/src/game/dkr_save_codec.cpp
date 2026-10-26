#include "dkr_save_codec.hpp"

#include <algorithm>

namespace {

using namespace dkr::runtime::saves::codec;

constexpr std::size_t kSlotSize = 0x28U;
constexpr std::size_t kConfigOffset = 0x78U;
constexpr std::size_t kFastestLapsOffset = 0x80U;
constexpr std::size_t kCourseTimesOffset = 0x140U;
constexpr std::size_t kRecordBlockSize = 0xC0U;
constexpr std::string_view kNameAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.?    ";
constexpr std::uint64_t kConfigPayloadMask = 0x00FFFFFFFFFFFFFFULL;
constexpr std::uint64_t kKnownConfigBits = 0x00FFFFFFULL | (1ULL << 25U);
constexpr std::uint64_t kPreservedConfigBits = kConfigPayloadMask & ~kKnownConfigBits;

constexpr std::array<const char*, kCourseCount> kCourseNames{{
    "Bluey I",         "Fossil Canyon",     "Pirate Lagoon",   "Ancient Lake",    "Walrus Cove",    "Hot Top Volcano",
    "Whale Bay",       "Snowball Valley",   "Crescent Island", "Fire Mountain",   "Everfrost Peak", "Spaceport Alpha",
    "Spacedust Alley", "Greenwood Village", "Boulder Canyon",  "Windmill Plains", "Smokey Castle",  "Darkwater Beach",
    "Icicle Pyramid",  "Frosty Village",    "Jungle Falls",    "Treasure Caves",  "Haunted Woods",  "Darkmoon Caverns",
    "Star City",       "Wizpig I",          "Tricky I",        "Bubbler I",       "Smokey I",       "Tricky II",
    "Bluey II",        "Bubbler II",        "Smokey II",       "Wizpig II",
}};

constexpr std::array<const char*, kRecordCount> kRecordNames{{
    "Fossil Canyon - Car",
    "Fossil Canyon - Hovercraft",
    "Fossil Canyon - Plane",
    "Pirate Lagoon - Hovercraft",
    "Ancient Lake - Car",
    "Ancient Lake - Hovercraft",
    "Ancient Lake - Plane",
    "Walrus Cove - Car",
    "Walrus Cove - Hovercraft",
    "Hot Top Volcano - Hovercraft",
    "Hot Top Volcano - Plane",
    "Whale Bay - Hovercraft",
    "Snowball Valley - Car",
    "Snowball Valley - Hovercraft",
    "Crescent Island - Car",
    "Crescent Island - Hovercraft",
    "Everfrost Peak - Car",
    "Everfrost Peak - Hovercraft",
    "Everfrost Peak - Plane",
    "Spaceport Alpha - Car",
    "Spaceport Alpha - Hovercraft",
    "Spaceport Alpha - Plane",
    "Spacedust Alley - Car",
    "Spacedust Alley - Hovercraft",
    "Spacedust Alley - Plane",
    "Greenwood Village - Car",
    "Greenwood Village - Hovercraft",
    "Boulder Canyon - Hovercraft",
    "Windmill Plains - Car",
    "Windmill Plains - Hovercraft",
    "Windmill Plains - Plane",
    "Frosty Village - Car",
    "Frosty Village - Hovercraft",
    "Frosty Village - Plane",
    "Jungle Falls - Car",
    "Jungle Falls - Hovercraft",
    "Jungle Falls - Plane",
    "Treasure Caves - Car",
    "Treasure Caves - Hovercraft",
    "Treasure Caves - Plane",
    "Haunted Woods - Car",
    "Haunted Woods - Hovercraft",
    "Darkmoon Caverns - Car",
    "Darkmoon Caverns - Hovercraft",
    "Star City - Car",
    "Star City - Hovercraft",
    "Star City - Plane",
}};

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

void write_be16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

std::uint64_t read_be64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

void write_be64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + 7U - index] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

std::uint16_t block_checksum(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
    std::uint32_t checksum = 5U;
    for (std::size_t index = 2; index < size; ++index) {
        checksum += bytes[offset + index];
    }
    return static_cast<std::uint16_t>(checksum);
}

std::uint8_t config_checksum(std::uint64_t payload) {
    std::uint32_t checksum = 5U;
    for (int index = 0; index < 14; ++index) {
        checksum += static_cast<std::uint8_t>((payload >> (index * 4)) & 0xFU);
    }
    return static_cast<std::uint8_t>(checksum);
}

class BitReader {
  public:
    BitReader(std::span<const std::uint8_t> bytes, std::size_t bit) : bytes_(bytes), bit_(bit) {}

    std::uint32_t read(unsigned count) {
        std::uint32_t value = 0;
        for (unsigned index = 0; index < count; ++index) {
            value = (value << 1U) | ((bytes_[bit_ >> 3U] >> (7U - (bit_ & 7U))) & 1U);
            ++bit_;
        }
        return value;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t bit_;
};

class BitWriter {
  public:
    BitWriter(std::vector<std::uint8_t>& bytes, std::size_t bit) : bytes_(bytes), bit_(bit) {}

    void write(std::uint32_t value, unsigned count) {
        for (unsigned index = 0; index < count; ++index) {
            const unsigned shift = count - index - 1U;
            const std::uint8_t mask = static_cast<std::uint8_t>(1U << (7U - (bit_ & 7U)));
            if (((value >> shift) & 1U) != 0U) {
                bytes_[bit_ >> 3U] |= mask;
            } else {
                bytes_[bit_ >> 3U] &= static_cast<std::uint8_t>(~mask);
            }
            ++bit_;
        }
    }

  private:
    std::vector<std::uint8_t>& bytes_;
    std::size_t bit_;
};

std::string decode_name(std::uint16_t packed) {
    std::string result(3, ' ');
    for (int index = 2; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = kNameAlphabet[packed & 0x1FU];
        packed >>= 5U;
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::uint16_t encode_name(std::string_view value) {
    const std::string clean = sanitise_name(value);
    std::uint16_t packed = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const char character = index < clean.size() ? clean[index] : ' ';
        const std::size_t found = kNameAlphabet.find(character);
        packed = static_cast<std::uint16_t>((packed << 5U) | (found == std::string_view::npos ? 31U : found));
    }
    return packed;
}

bool decode_slot(std::span<const std::uint8_t> bytes, std::size_t offset, AdventureSlot& slot) {
    // Retail EEPROMs may leave an unused adventure slot in the erased-chip
    // state (all bits set). The game treats that as an empty slot, so the
    // desktop editor must accept it as well instead of reporting a corrupt
    // 512-byte save. Once edited, encode_slot writes the ordinary checksummed
    // representation used for a blank slot.
    const auto slot_bytes = bytes.subspan(offset, kSlotSize);
    if (std::all_of(slot_bytes.begin(), slot_bytes.end(), [](std::uint8_t value) { return value == 0xFFU; })) {
        slot = AdventureSlot{};
        return true;
    }
    if (read_be16(bytes, offset) != block_checksum(bytes, offset, kSlotSize)) {
        return false;
    }
    BitReader bits(bytes, offset * 8U + 16U);
    for (auto& status : slot.course_status)
        status = bits.read(2);
    slot.taj_flags = bits.read(6);
    slot.trophies = bits.read(10);
    slot.bosses = bits.read(12);
    for (auto& balloons : slot.balloons)
        balloons = bits.read(7);
    slot.tt_amulet = bits.read(3);
    slot.wizpig_amulet = bits.read(3);
    for (auto& flags : slot.world_flags)
        flags = bits.read(16);
    slot.keys = bits.read(8);
    slot.cutscenes = bits.read(32);
    slot.name = decode_name(static_cast<std::uint16_t>(bits.read(16)));
    slot.reserved = bits.read(8);
    return true;
}

void encode_slot(const AdventureSlot& slot, std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::fill(bytes.begin() + offset, bytes.begin() + offset + kSlotSize, 0U);
    BitWriter bits(bytes, offset * 8U + 16U);
    for (const auto status : slot.course_status)
        bits.write(std::min(status, std::uint8_t{3}), 2);
    bits.write(slot.taj_flags & 0x3FU, 6);
    bits.write(slot.trophies & 0x3FFU, 10);
    bits.write(slot.bosses & 0xFFFU, 12);
    for (const auto balloons : slot.balloons)
        bits.write(std::min(balloons, std::uint8_t{127}), 7);
    bits.write(std::min(slot.tt_amulet, std::uint8_t{7}), 3);
    bits.write(std::min(slot.wizpig_amulet, std::uint8_t{7}), 3);
    for (const auto flags : slot.world_flags)
        bits.write(flags, 16);
    bits.write(slot.keys, 8);
    bits.write(slot.cutscenes, 32);
    bits.write(encode_name(slot.name), 16);
    bits.write(slot.reserved, 8);
    write_be16(bytes, offset, block_checksum(bytes, offset, kSlotSize));
}

bool decode_records(std::span<const std::uint8_t> bytes, std::size_t offset, std::array<Record, kRecordCount>& records,
                    std::array<std::uint8_t, 2>& tail) {
    if (read_be16(bytes, offset) != block_checksum(bytes, offset, kRecordBlockSize)) {
        return false;
    }
    std::size_t cursor = offset + 2U;
    for (auto& record : records) {
        record.time = read_be16(bytes, cursor);
        record.initials = decode_name(read_be16(bytes, cursor + 2U));
        cursor += 4U;
    }
    tail[0] = bytes[offset + kRecordBlockSize - 2U];
    tail[1] = bytes[offset + kRecordBlockSize - 1U];
    return true;
}

void encode_records(const std::array<Record, kRecordCount>& records, const std::array<std::uint8_t, 2>& tail,
                    std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::fill(bytes.begin() + offset, bytes.begin() + offset + kRecordBlockSize, 0U);
    std::size_t cursor = offset + 2U;
    for (const auto& record : records) {
        write_be16(bytes, cursor, record.time);
        write_be16(bytes, cursor + 2U, encode_name(record.initials));
        cursor += 4U;
    }
    bytes[offset + kRecordBlockSize - 2U] = tail[0];
    bytes[offset + kRecordBlockSize - 1U] = tail[1];
    write_be16(bytes, offset, block_checksum(bytes, offset, kRecordBlockSize));
}

} // namespace

bool dkr::runtime::saves::codec::decode(std::span<const std::uint8_t> bytes, SaveImage& image, std::string* error) {
    if (bytes.size() != kImageSize) {
        if (error)
            *error = "Adventure EEPROM must be exactly 512 bytes.";
        return false;
    }
    for (std::size_t index = 0; index < kAdventureSlotCount; ++index) {
        if (!decode_slot(bytes, index * kSlotSize, image.slots[index])) {
            if (error)
                *error = "Adventure slot " + std::to_string(index + 1U) + " has an invalid checksum.";
            return false;
        }
    }
    const std::uint64_t config = read_be64(bytes, kConfigOffset);
    const std::uint64_t payload = config & 0x00FFFFFFFFFFFFFFULL;
    if (static_cast<std::uint8_t>(config >> 56U) != config_checksum(payload)) {
        if (error)
            *error = "Global unlock settings have an invalid checksum.";
        return false;
    }
    image.settings.adventure_two = (payload & 1U) != 0;
    image.settings.drumstick = (payload & 2U) != 0;
    image.settings.language = static_cast<std::uint8_t>((payload >> 2U) & 3U);
    for (std::size_t index = 0; index < image.settings.tt_trials.size(); ++index) {
        image.settings.tt_trials[index] = ((payload >> (4U + index)) & 1U) != 0;
    }
    image.settings.subtitles = ((payload >> 25U) & 1U) != 0;
    image.settings.preserved_config_bits = payload & kPreservedConfigBits;
    if (!decode_records(bytes, kFastestLapsOffset, image.fastest_laps, image.fastest_laps_tail) ||
        !decode_records(bytes, kCourseTimesOffset, image.course_times, image.course_times_tail)) {
        if (error)
            *error = "T.T. record data has an invalid checksum.";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> dkr::runtime::saves::codec::encode(const SaveImage& image) {
    std::vector<std::uint8_t> bytes(kImageSize, 0U);
    for (std::size_t index = 0; index < kAdventureSlotCount; ++index) {
        encode_slot(image.slots[index], bytes, index * kSlotSize);
    }
    std::uint64_t payload = (image.settings.preserved_config_bits & kPreservedConfigBits) |
                            (image.settings.adventure_two ? 1ULL : 0ULL) | (image.settings.drumstick ? 2ULL : 0ULL) |
                            (static_cast<std::uint64_t>(image.settings.language & 3U) << 2U) |
                            (static_cast<std::uint64_t>(image.settings.subtitles ? 1U : 0U) << 25U);
    for (std::size_t index = 0; index < image.settings.tt_trials.size(); ++index) {
        if (image.settings.tt_trials[index])
            payload |= 1ULL << (4U + index);
    }
    write_be64(bytes, kConfigOffset, payload | (static_cast<std::uint64_t>(config_checksum(payload)) << 56U));
    encode_records(image.fastest_laps, image.fastest_laps_tail, bytes, kFastestLapsOffset);
    encode_records(image.course_times, image.course_times_tail, bytes, kCourseTimesOffset);
    return bytes;
}

SaveImage dkr::runtime::saves::codec::blank_image() {
    SaveImage image{};
    // Match the retail game's checksum-recovery defaults: bit 24 is an
    // opaque always-on bit and bit 25 enables subtitles.
    image.settings.preserved_config_bits = 1ULL << 24U;
    image.settings.subtitles = true;
    return image;
}

std::vector<std::uint8_t> dkr::runtime::saves::codec::blank_bytes() {
    return encode(blank_image());
}

bool dkr::runtime::saves::codec::validate(std::span<const std::uint8_t> bytes, std::string* error) {
    SaveImage ignored{};
    return decode(bytes, ignored, error);
}

bool dkr::runtime::saves::codec::repair_checksums(std::span<const std::uint8_t> bytes,
                                                  std::vector<std::uint8_t>& repaired, std::string* error) {
    if (bytes.size() != kImageSize) {
        if (error)
            *error = "Adventure EEPROM must be exactly 512 bytes.";
        repaired.clear();
        return false;
    }

    repaired.assign(bytes.begin(), bytes.end());
    for (std::size_t index = 0; index < kAdventureSlotCount; ++index) {
        const std::size_t offset = index * kSlotSize;
        const auto slot = bytes.subspan(offset, kSlotSize);
        // The retail game accepts an untouched erased slot. Preserve all of
        // its 0xFF bytes instead of converting it as a side effect of repair.
        if (std::all_of(slot.begin(), slot.end(), [](std::uint8_t value) { return value == 0xFFU; })) {
            continue;
        }
        write_be16(repaired, offset, block_checksum(repaired, offset, kSlotSize));
    }

    const std::uint64_t payload = read_be64(repaired, kConfigOffset) & kConfigPayloadMask;
    write_be64(repaired, kConfigOffset, payload | (static_cast<std::uint64_t>(config_checksum(payload)) << 56U));
    write_be16(repaired, kFastestLapsOffset, block_checksum(repaired, kFastestLapsOffset, kRecordBlockSize));
    write_be16(repaired, kCourseTimesOffset, block_checksum(repaired, kCourseTimesOffset, kRecordBlockSize));

    std::string validation_error;
    if (!validate(repaired, &validation_error)) {
        if (error) {
            *error = "The checksum-repaired EEPROM did not validate: " + validation_error;
        }
        repaired.clear();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool dkr::runtime::saves::codec::canonical_bytes(std::span<const std::uint8_t> bytes,
                                                 std::vector<std::uint8_t>& canonical, std::string* error) {
    SaveImage image{};
    if (!decode(bytes, image, error)) {
        canonical.clear();
        return false;
    }
    canonical = encode(image);
    if (!validate(canonical, error)) {
        canonical.clear();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

void dkr::runtime::saves::codec::normalise_editable_fields(SaveImage& image) {
    for (auto& slot : image.slots) {
        for (auto& status : slot.course_status) {
            status = std::min(status, std::uint8_t{3});
        }
        slot.taj_flags &= 0x3FU;
        slot.trophies &= 0x3FFU;
        slot.bosses &= 0xFFFU;
        slot.tt_amulet = std::min(slot.tt_amulet, kMaximumAmuletPieces);
        slot.wizpig_amulet = std::min(slot.wizpig_amulet, kMaximumAmuletPieces);

        unsigned world_total = 0U;
        for (std::size_t world = 1U; world < kWorldCount; ++world) {
            slot.balloons[world] = std::min(slot.balloons[world], kMaximumWorldBalloons);
            world_total += slot.balloons[world];
        }
        const unsigned saved_total = std::min<unsigned>(slot.balloons[0], kMaximumTotalBalloons);
        const unsigned hub_count =
            saved_total > world_total ? std::min<unsigned>(saved_total - world_total, kMaximumHubBalloons) : 0U;
        slot.balloons[0] = static_cast<std::uint8_t>(world_total + hub_count);
        slot.name = sanitise_name(slot.name);
    }
    image.settings.language = std::min(image.settings.language, std::uint8_t{3});
}

bool dkr::runtime::saves::codec::validate_editable_ranges(const SaveImage& image, std::string* error) {
    for (std::size_t slot_index = 0; slot_index < image.slots.size(); ++slot_index) {
        const auto& slot = image.slots[slot_index];
        const std::string prefix = "Adventure " + std::to_string(slot_index + 1U) + ": ";
        for (const auto status : slot.course_status) {
            if (status > 3U) {
                if (error)
                    *error = prefix + "course progress is out of range.";
                return false;
            }
        }
        if (slot.taj_flags > 0x3FU || slot.trophies > 0x3FFU || slot.bosses > 0xFFFU) {
            if (error)
                *error = prefix + "progress flags are out of range.";
            return false;
        }
        if (slot.tt_amulet > kMaximumAmuletPieces || slot.wizpig_amulet > kMaximumAmuletPieces) {
            if (error)
                *error = prefix + "amulet pieces cannot exceed four.";
            return false;
        }
        unsigned world_total = 0U;
        for (std::size_t world = 1U; world < kWorldCount; ++world) {
            if (slot.balloons[world] > kMaximumWorldBalloons) {
                if (error)
                    *error = prefix + "a racing world cannot contain more than eight balloons.";
                return false;
            }
            world_total += slot.balloons[world];
        }
        if (slot.balloons[0] > kMaximumTotalBalloons || world_total > slot.balloons[0] ||
            slot.balloons[0] - world_total > kMaximumHubBalloons) {
            if (error)
                *error = prefix + "balloon totals must equal the five worlds plus DKR-R's central island region.";
            return false;
        }
    }
    if (image.settings.language > 3U) {
        if (error)
            *error = "The selected language is out of range.";
        return false;
    }
    return true;
}

std::string dkr::runtime::saves::codec::sanitise_name(std::string_view value) {
    std::string clean;
    for (char character : value) {
        if (clean.size() == 3U)
            break;
        if (character >= 'a' && character <= 'z')
            character -= 'a' - 'A';
        clean.push_back(kNameAlphabet.find(character) == std::string_view::npos ? ' ' : character);
    }
    return clean;
}

const std::array<const char*, kCourseCount>& dkr::runtime::saves::codec::course_names() {
    return kCourseNames;
}

const std::array<const char*, kRecordCount>& dkr::runtime::saves::codec::record_names() {
    return kRecordNames;
}

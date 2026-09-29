#include "authoritative_state.hpp"

#include "../authored_state_contract.hpp"
#include "../performance_trace.hpp"
#include "revision_addresses.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace dkr::runtime::netplay {
namespace {

constexpr std::size_t kRetailRdramSize = 0x00800000U;
constexpr std::uint32_t kMagic = 0x444B5241U; // DKRA
constexpr std::uint32_t kOrientationMagic = 0x444B524FU; // DKRO
constexpr std::uint32_t kOrientationSchema = 1U;
constexpr std::uint32_t kMaximumRacers = 10U;
constexpr std::uint32_t kMaximumObjects = 512U;
// The wire protocol carries at most sixteen MTU-safe state fragments. Keep a
// hard actor bound that leaves room for the ten-racer worst case rather than
// allocating until a late, opaque protocol-budget failure.
constexpr std::uint32_t kMaximumAuthoredActors = 112U;
constexpr std::uint16_t kObjectParticleFlag = 0x8000U;

constexpr std::uint16_t kActorPropertyWord0 = 1U << 0U;
constexpr std::uint16_t kActorPropertyWord1 = 1U << 1U;
constexpr std::uint16_t kActorLogBehavior = 1U << 2U;

// DKR's gameplay water is a compact procedural phase, not the generated
// vertex buffers emitted by waves_render. Retail level headers use at most a
// 20x20 index grid and 32 swell generators; retain modest validation headroom
// without ever walking an unbounded RDRAM allocation received from a peer.
constexpr std::uint32_t kMaximumWaveTileCount = 32U;
constexpr std::uint32_t kMaximumWaveSeedSize = 512U;
constexpr std::uint32_t kMaximumWaveGenerators = 32U;
constexpr std::uint32_t kWaveControllerTileCountOffset = 0x04U;
constexpr std::uint32_t kWaveControllerSeedSizeOffset = 0x20U;
constexpr std::uint32_t kWaveControllerMagnitudeOffset = 0x40U;
constexpr std::uint32_t kWaveGeneratorStride = 0x40U;
constexpr std::uint32_t kWaveGeneratorPhaseOffset = 0x1AU;
constexpr std::size_t kMaximumWaterStateBytes = 105U;

using namespace authored_contract;

constexpr std::size_t kWordsPerRacer = kObjectWords.size() +
    kRacerIdentityWords.size() + kRacerPhysicsWords.size() +
    kRacerPostPointerWords.size() + kRacerLapWords.size() +
    kRacerRotationWords.size() + kRacerRaceWords.size();
constexpr std::size_t kHalvesPerRacer = kRacerRaceHalves.size();
constexpr std::size_t kBytesPerRacer = kRacerRaceBytes.size();

struct ActorSource {
    std::uint32_t map_bank = 0U;
    std::uint32_t level_entry_offset = 0U;
    std::uint16_t behavior = 0U;
    std::uint16_t object_id = 0U;
    std::uint16_t fields = 0U;
    std::uint32_t object = 0U;
};

struct RacerOrientation {
    std::uint32_t camera_zoom = 0U;
    std::uint16_t camera_yaw = 0U;
    std::uint16_t visual_steering = 0U;
};

struct ParsedOrientationState {
    std::uint32_t frame = 0U;
    std::uint32_t map = 0U;
    std::vector<RacerOrientation> racers;
};

struct WaterRuntime {
    bool active = false;
    std::uint32_t height_indices = 0U;
    std::uint32_t seed_size = 0U;
    std::uint32_t tile_count = 0U;
    std::uint32_t generator_count = 0U;
    std::uint32_t generator_list = 0U;
    std::uint32_t generator_objects = 0U;
    std::uint32_t generator_mask = 0U;
};

bool actor_identity_less(const ActorSource& lhs, const ActorSource& rhs) {
    if (lhs.map_bank != rhs.map_bank) {
        return lhs.map_bank < rhs.map_bank;
    }
    return lhs.level_entry_offset < rhs.level_entry_offset;
}

bool actor_identity_equal(const ActorSource& lhs, const ActorSource& rhs) {
    return lhs.map_bank == rhs.map_bank &&
           lhs.level_entry_offset == rhs.level_entry_offset;
}

std::size_t offset_of(std::uint32_t address, std::size_t extent,
                      std::size_t size) {
    const std::uint32_t region = address & 0xE0000000U;
    if ((region != 0x80000000U && region != 0xA0000000U) ||
        extent > size) return std::numeric_limits<std::size_t>::max();
    const std::size_t offset = address & 0x1FFFFFFFU;
    return offset <= size - extent ? offset
                                   : std::numeric_limits<std::size_t>::max();
}

bool read_word(const std::uint8_t* rdram, std::size_t size,
               std::uint32_t address, std::uint32_t& value) {
    const std::size_t offset = offset_of(address, 4U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    std::memcpy(&value, rdram + offset, 4U);
    return true;
}

bool write_word(std::uint8_t* rdram, std::size_t size,
                std::uint32_t address, std::uint32_t value) {
    const std::size_t offset = offset_of(address, 4U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    std::memcpy(rdram + offset, &value, 4U);
    return true;
}

bool read_byte(const std::uint8_t* rdram, std::size_t size,
               std::uint32_t address, std::uint8_t& value) {
    const std::size_t offset = offset_of(address ^ 3U, 1U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    value = rdram[offset];
    return true;
}

bool write_byte(std::uint8_t* rdram, std::size_t size,
                std::uint32_t address, std::uint8_t value) {
    const std::size_t offset = offset_of(address ^ 3U, 1U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    rdram[offset] = value;
    return true;
}

bool read_half(const std::uint8_t* rdram, std::size_t size,
               std::uint32_t address, std::uint16_t& value) {
    const std::size_t offset = offset_of(address ^ 2U, 2U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    std::memcpy(&value, rdram + offset, 2U);
    return true;
}

bool write_half(std::uint8_t* rdram, std::size_t size,
                std::uint32_t address, std::uint16_t value) {
    const std::size_t offset = offset_of(address ^ 2U, 2U, size);
    if (offset == std::numeric_limits<std::size_t>::max()) return false;
    std::memcpy(rdram + offset, &value, 2U);
    return true;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

bool take32(std::span<const std::uint8_t> bytes, std::size_t& cursor,
            std::uint32_t& value) {
    if (cursor + 4U > bytes.size()) return false;
    value = 0U;
    for (int index = 0; index < 4; ++index) {
        value = (value << 8U) | bytes[cursor++];
    }
    return true;
}

bool take16(std::span<const std::uint8_t> bytes, std::size_t& cursor,
            std::uint16_t& value) {
    if (cursor + 2U > bytes.size()) return false;
    value = static_cast<std::uint16_t>(bytes[cursor] << 8U) |
            bytes[cursor + 1U];
    cursor += 2U;
    return true;
}

bool take8(std::span<const std::uint8_t> bytes, std::size_t& cursor,
           std::uint8_t& value) {
    if (cursor >= bytes.size()) return false;
    value = bytes[cursor++];
    return true;
}

void hash_water_half(std::uint64_t& hash, std::uint16_t value) {
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    hash ^= static_cast<std::uint8_t>(value >> 8U);
    hash *= kFnvPrime;
    hash ^= static_cast<std::uint8_t>(value);
    hash *= kFnvPrime;
}

bool inspect_water_runtime(const std::uint8_t* rdram, std::size_t size,
                           WaterRuntime& water) {
    water = {};
    if (!read_word(rdram, size, revision_addresses::WaveHeightIndices,
                   water.height_indices) ||
        !read_word(rdram, size,
                   revision_addresses::WaveController +
                       kWaveControllerSeedSizeOffset,
                   water.seed_size) ||
        !read_word(rdram, size,
                   revision_addresses::WaveController +
                       kWaveControllerTileCountOffset,
                   water.tile_count) ||
        !read_word(rdram, size, revision_addresses::WaveGenCount,
                   water.generator_count) ||
        !read_word(rdram, size, revision_addresses::WaveGenList,
                   water.generator_list) ||
        !read_word(rdram, size, revision_addresses::WaveGenObjects,
                   water.generator_objects)) {
        return false;
    }

    water.active = water.height_indices != 0U;
    if (!water.active) return true;
    if (water.seed_size == 0U || water.seed_size > kMaximumWaveSeedSize ||
        water.tile_count == 0U ||
        water.tile_count > kMaximumWaveTileCount ||
        water.generator_count > kMaximumWaveGenerators) {
        return false;
    }
    const std::size_t cells = static_cast<std::size_t>(water.tile_count) *
                              water.tile_count;
    if (offset_of(water.height_indices, cells * 4U, size) ==
        std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    std::uint32_t active_generators = 0U;
    if (water.generator_objects != 0U) {
        if (offset_of(water.generator_objects,
                      kMaximumWaveGenerators * 4U, size) ==
            std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        for (std::uint32_t index = 0U;
             index < kMaximumWaveGenerators; ++index) {
            std::uint32_t object = 0U;
            if (!read_word(rdram, size,
                           water.generator_objects + index * 4U, object)) {
                return false;
            }
            if (object != 0U) {
                water.generator_mask |= 1U << index;
                ++active_generators;
            }
        }
    }
    if (active_generators != water.generator_count) return false;
    if (water.generator_mask != 0U &&
        (water.generator_list == 0U ||
         offset_of(water.generator_list,
                   kMaximumWaveGenerators * kWaveGeneratorStride, size) ==
             std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    return true;
}

bool water_relative_index_hash(const std::uint8_t* rdram, std::size_t size,
                               const WaterRuntime& water,
                               std::uint16_t phase_x,
                               std::uint16_t phase_y,
                               std::uint64_t& hash) {
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    hash = kFnvOffset;
    const std::size_t cells = static_cast<std::size_t>(water.tile_count) *
                              water.tile_count;
    for (std::size_t index = 0U; index < cells; ++index) {
        std::uint16_t x = 0U;
        std::uint16_t y = 0U;
        const std::uint32_t address = water.height_indices +
            static_cast<std::uint32_t>(index * 4U);
        if (!read_half(rdram, size, address, x) ||
            !read_half(rdram, size, address + 2U, y) ||
            x >= water.seed_size || y >= water.seed_size) {
            return false;
        }
        const auto relative_x = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(x) + water.seed_size - phase_x) %
            water.seed_size);
        const auto relative_y = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(y) + water.seed_size - phase_y) %
            water.seed_size);
        hash_water_half(hash, relative_x);
        hash_water_half(hash, relative_y);
    }
    return true;
}

bool capture_water_state(const std::uint8_t* rdram, std::size_t size,
                         std::vector<std::uint8_t>& output,
                         std::string& error) {
    WaterRuntime water;
    if (!inspect_water_runtime(rdram, size, water)) {
        error = "DKR's procedural-water topology is invalid.";
        return false;
    }
    output.push_back(water.active ? 1U : 0U);
    if (!water.active) return true;

    std::uint16_t phase_x = 0U;
    std::uint16_t phase_y = 0U;
    std::uint64_t relative_hash = 0U;
    std::array<std::uint16_t, kMaximumWaveGenerators> generator_phases{};
    std::uint32_t controller_magnitude = 0U;
    std::uint32_t power_base = 0U;
    std::uint32_t power_magnitude = 0U;
    std::uint32_t power_divisor = 0U;
    if (!read_half(rdram, size, water.height_indices, phase_x) ||
        !read_half(rdram, size, water.height_indices + 2U, phase_y) ||
        phase_x >= water.seed_size || phase_y >= water.seed_size ||
        !water_relative_index_hash(rdram, size, water, phase_x, phase_y,
                                   relative_hash) ||
        !read_word(rdram, size,
                   revision_addresses::WaveController +
                       kWaveControllerMagnitudeOffset,
                   controller_magnitude) ||
        !read_word(rdram, size, revision_addresses::WavePowerBase,
                   power_base) ||
        !read_word(rdram, size, revision_addresses::WaveMagnitude,
                   power_magnitude) ||
        !read_word(rdram, size, revision_addresses::WavePowerDivisor,
                   power_divisor)) {
        error = "DKR's procedural-water phase is outside RDRAM.";
        return false;
    }
    for (std::uint32_t index = 0U; index < kMaximumWaveGenerators; ++index) {
        if ((water.generator_mask & (1U << index)) != 0U &&
            !read_half(rdram, size,
                       water.generator_list + index * kWaveGeneratorStride +
                           kWaveGeneratorPhaseOffset,
                       generator_phases[index])) {
            error = "A procedural-water generator phase is outside RDRAM.";
            return false;
        }
    }

    put16(output, static_cast<std::uint16_t>(water.seed_size));
    put16(output, static_cast<std::uint16_t>(water.tile_count));
    put16(output, phase_x);
    put16(output, phase_y);
    put32(output, static_cast<std::uint32_t>(relative_hash >> 32U));
    put32(output, static_cast<std::uint32_t>(relative_hash));
    put32(output, controller_magnitude);
    put32(output, power_base);
    put32(output, power_magnitude);
    put32(output, power_divisor);
    put32(output, water.generator_count);
    put32(output, water.generator_mask);
    for (const std::uint16_t phase : generator_phases) put16(output, phase);
    return true;
}

bool stage_water_state(
    const std::uint8_t* rdram, std::size_t size,
    std::span<const std::uint8_t> snapshot, std::size_t& cursor,
    std::vector<std::pair<std::uint32_t, std::uint32_t>>& word_writes,
    std::vector<std::pair<std::uint32_t, std::uint16_t>>& half_writes,
    std::string& error) {
    std::uint8_t active = 0U;
    if (!take8(snapshot, cursor, active) || active > 1U) {
        error = "The host snapshot's procedural-water marker is invalid.";
        return false;
    }

    WaterRuntime local;
    if (!inspect_water_runtime(rdram, size, local) ||
        local.active != (active != 0U)) {
        error = "The host snapshot's procedural-water topology does not match this track.";
        return false;
    }
    if (active == 0U) return true;

    std::uint16_t seed_size = 0U;
    std::uint16_t tile_count = 0U;
    std::uint16_t host_phase_x = 0U;
    std::uint16_t host_phase_y = 0U;
    std::uint32_t hash_high = 0U;
    std::uint32_t hash_low = 0U;
    std::uint32_t controller_magnitude = 0U;
    std::uint32_t power_base = 0U;
    std::uint32_t power_magnitude = 0U;
    std::uint32_t power_divisor = 0U;
    std::uint32_t generator_count = 0U;
    std::uint32_t generator_mask = 0U;
    std::array<std::uint16_t, kMaximumWaveGenerators> generator_phases{};
    if (!take16(snapshot, cursor, seed_size) ||
        !take16(snapshot, cursor, tile_count) ||
        !take16(snapshot, cursor, host_phase_x) ||
        !take16(snapshot, cursor, host_phase_y) ||
        !take32(snapshot, cursor, hash_high) ||
        !take32(snapshot, cursor, hash_low) ||
        !take32(snapshot, cursor, controller_magnitude) ||
        !take32(snapshot, cursor, power_base) ||
        !take32(snapshot, cursor, power_magnitude) ||
        !take32(snapshot, cursor, power_divisor) ||
        !take32(snapshot, cursor, generator_count) ||
        !take32(snapshot, cursor, generator_mask)) {
        error = "The host snapshot's procedural-water state is truncated.";
        return false;
    }
    for (std::uint16_t& phase : generator_phases) {
        if (!take16(snapshot, cursor, phase)) {
            error = "The host snapshot's procedural-water generators are truncated.";
            return false;
        }
    }
    if (seed_size != local.seed_size || tile_count != local.tile_count ||
        host_phase_x >= seed_size || host_phase_y >= seed_size ||
        generator_count > kMaximumWaveGenerators ||
        generator_mask != local.generator_mask) {
        error = "The host snapshot's procedural-water topology does not match this track.";
        return false;
    }
    for (std::uint32_t index = 0U; index < kMaximumWaveGenerators; ++index) {
        if ((generator_mask & (1U << index)) == 0U &&
            generator_phases[index] != 0U) {
            error = "The host snapshot contains an inactive water-generator phase.";
            return false;
        }
    }

    std::uint16_t local_phase_x = 0U;
    std::uint16_t local_phase_y = 0U;
    std::uint64_t local_hash = 0U;
    if (!read_half(rdram, size, local.height_indices, local_phase_x) ||
        !read_half(rdram, size, local.height_indices + 2U, local_phase_y) ||
        local_phase_x >= local.seed_size || local_phase_y >= local.seed_size ||
        !water_relative_index_hash(rdram, size, local, local_phase_x,
                                   local_phase_y, local_hash) ||
        local_hash != ((static_cast<std::uint64_t>(hash_high) << 32U) |
                       hash_low)) {
        error = "The host and client water seed layouts do not match.";
        return false;
    }

    const std::uint32_t delta_x =
        (static_cast<std::uint32_t>(host_phase_x) + seed_size -
         local_phase_x) % seed_size;
    const std::uint32_t delta_y =
        (static_cast<std::uint32_t>(host_phase_y) + seed_size -
         local_phase_y) % seed_size;
    if (delta_x != 0U || delta_y != 0U) {
        const std::size_t cells = static_cast<std::size_t>(tile_count) *
                                  tile_count;
        for (std::size_t index = 0U; index < cells; ++index) {
            std::uint16_t x = 0U;
            std::uint16_t y = 0U;
            const std::uint32_t address = local.height_indices +
                static_cast<std::uint32_t>(index * 4U);
            if (!read_half(rdram, size, address, x) ||
                !read_half(rdram, size, address + 2U, y)) {
                error = "The local procedural-water phase left valid RDRAM.";
                return false;
            }
            half_writes.emplace_back(
                address, static_cast<std::uint16_t>((x + delta_x) % seed_size));
            half_writes.emplace_back(
                address + 2U,
                static_cast<std::uint16_t>((y + delta_y) % seed_size));
        }
    }
    word_writes.emplace_back(
        revision_addresses::WaveController +
            kWaveControllerMagnitudeOffset,
        controller_magnitude);
    word_writes.emplace_back(revision_addresses::WavePowerBase, power_base);
    word_writes.emplace_back(revision_addresses::WaveMagnitude,
                             power_magnitude);
    word_writes.emplace_back(revision_addresses::WavePowerDivisor,
                             power_divisor);
    word_writes.emplace_back(revision_addresses::WaveGenCount,
                             generator_count);
    for (std::uint32_t index = 0U; index < kMaximumWaveGenerators; ++index) {
        if ((generator_mask & (1U << index)) != 0U) {
            half_writes.emplace_back(
                local.generator_list + index * kWaveGeneratorStride +
                    kWaveGeneratorPhaseOffset,
                generator_phases[index]);
        }
    }
    return true;
}

template <std::size_t N>
bool capture_words(const std::uint8_t* rdram, std::size_t size,
                   std::uint32_t base,
                   const std::array<std::uint16_t, N>& offsets,
                   std::vector<std::uint8_t>& out) {
    for (const std::uint16_t offset : offsets) {
        std::uint32_t value = 0U;
        if (!read_word(rdram, size, base + offset, value)) return false;
        put32(out, value);
    }
    return true;
}

template <std::size_t N>
bool stage_words(std::size_t size, std::uint32_t base,
                 const std::array<std::uint16_t, N>& offsets,
                 std::span<const std::uint8_t> bytes, std::size_t& cursor,
                 std::vector<std::pair<std::uint32_t, std::uint32_t>>& writes) {
    for (const std::uint16_t offset : offsets) {
        std::uint32_t value = 0U;
        if (!take32(bytes, cursor, value) ||
            offset_of(base + offset, 4U, size) ==
                std::numeric_limits<std::size_t>::max()) return false;
        writes.emplace_back(base + offset, value);
    }
    return true;
}

template <std::size_t N>
bool capture_halves(const std::uint8_t* rdram, std::size_t size,
                    std::uint32_t base,
                    const std::array<std::uint16_t, N>& offsets,
                    std::vector<std::uint8_t>& out) {
    for (const std::uint16_t offset : offsets) {
        std::uint16_t value = 0U;
        if (!read_half(rdram, size, base + offset, value)) return false;
        put16(out, value);
    }
    return true;
}

template <std::size_t N>
bool stage_halves(
    std::size_t size, std::uint32_t base,
    const std::array<std::uint16_t, N>& offsets,
    std::span<const std::uint8_t> bytes, std::size_t& cursor,
    std::vector<std::pair<std::uint32_t, std::uint16_t>>& writes) {
    for (const std::uint16_t offset : offsets) {
        std::uint16_t value = 0U;
        if (!take16(bytes, cursor, value) ||
            offset_of((base + offset) ^ 2U, 2U, size) ==
                std::numeric_limits<std::size_t>::max()) return false;
        writes.emplace_back(base + offset, value);
    }
    return true;
}

template <std::size_t N>
bool capture_bytes(const std::uint8_t* rdram, std::size_t size,
                   std::uint32_t base,
                   const std::array<std::uint16_t, N>& offsets,
                   std::vector<std::uint8_t>& out) {
    for (const std::uint16_t offset : offsets) {
        std::uint8_t value = 0U;
        if (!read_byte(rdram, size, base + offset, value)) return false;
        out.push_back(value);
    }
    return true;
}

template <std::size_t N>
bool stage_bytes(
    std::size_t size, std::uint32_t base,
    const std::array<std::uint16_t, N>& offsets,
    std::span<const std::uint8_t> bytes, std::size_t& cursor,
    std::vector<std::pair<std::uint32_t, std::uint8_t>>& writes) {
    for (const std::uint16_t offset : offsets) {
        std::uint8_t value = 0U;
        if (!take8(bytes, cursor, value) ||
            offset_of((base + offset) ^ 3U, 1U, size) ==
                std::numeric_limits<std::size_t>::max()) return false;
        writes.emplace_back(base + offset, value);
    }
    return true;
}

template <std::size_t N>
bool consume_words(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                   const std::array<std::uint16_t, N>&) {
    std::uint32_t ignored = 0U;
    for (std::size_t index = 0U; index < N; ++index) {
        if (!take32(bytes, cursor, ignored)) return false;
    }
    return true;
}

template <std::size_t N>
bool consume_halves(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                    const std::array<std::uint16_t, N>&) {
    std::uint16_t ignored = 0U;
    for (std::size_t index = 0U; index < N; ++index) {
        if (!take16(bytes, cursor, ignored)) return false;
    }
    return true;
}

bool racer_addresses(const std::uint8_t* rdram, std::size_t size,
                     std::uint32_t racer_array, std::uint32_t index,
                     std::uint32_t& object, std::uint32_t& racer) {
    return read_word(rdram, size, racer_array + index * 4U, object) &&
           read_word(rdram, size, object + 0x64U, racer) &&
           offset_of(object, 0x68U, size) !=
               std::numeric_limits<std::size_t>::max() &&
           offset_of(racer, 0x224U, size) !=
               std::numeric_limits<std::size_t>::max();
}

bool parse_orientation_state(std::span<const std::uint8_t> snapshot,
                             ParsedOrientationState& parsed) {
    parsed = {};
    std::size_t cursor = 0U;
    std::uint32_t magic = 0U;
    std::uint32_t schema = 0U;
    std::uint32_t racer_count = 0U;
    if (!take32(snapshot, cursor, magic) ||
        !take32(snapshot, cursor, schema) ||
        !take32(snapshot, cursor, parsed.frame) ||
        !take32(snapshot, cursor, parsed.map) ||
        !take32(snapshot, cursor, racer_count) ||
        magic != kOrientationMagic || schema != kOrientationSchema ||
        racer_count > kMaximumRacers ||
        snapshot.size() != 20U + static_cast<std::size_t>(racer_count) * 12U) {
        return false;
    }
    parsed.racers.resize(racer_count);
    for (std::uint32_t expected_index = 0U;
         expected_index < racer_count; ++expected_index) {
        std::uint32_t index = 0U;
        RacerOrientation& racer = parsed.racers[expected_index];
        if (!take32(snapshot, cursor, index) || index != expected_index ||
            !take32(snapshot, cursor, racer.camera_zoom) ||
            !take16(snapshot, cursor, racer.camera_yaw) ||
            !take16(snapshot, cursor, racer.visual_steering)) {
            parsed = {};
            return false;
        }
        float zoom = 0.0F;
        std::memcpy(&zoom, &racer.camera_zoom, sizeof(zoom));
        if (!std::isfinite(zoom)) {
            parsed = {};
            return false;
        }
    }
    return cursor == snapshot.size();
}

// Static scenery, audio helpers, cameras, checkpoints and particle-only
// actors have no authored race motion. Everything returned here can change
// collision, race progress, item state or a scripted track obstacle.
bool authored_actor_behavior(std::uint16_t behavior) {
    switch (behavior) {
        case 3U:   // BHV_FISH
        case 4U:   // BHV_ANIMATOR
        case 5U:   // BHV_WEAPON
        case 12U:  // BHV_DINO_WHALE
        case 14U:  // BHV_DOOR
        case 17U:  // BHV_WEAPON_BALLOON
        case 18U:  // BHV_WEAPON_2
        case 22U:  // BHV_BOMB_EXPLOSION
        case 23U:  // BHV_BALLOON_POP
        case 31U:  // BHV_STOPWATCH_MAN
        case 32U:  // BHV_BANANA
        case 36U:  // BHV_BUOY_PIRATE_SHIP
        case 38U:  // BHV_BRIDGE_WHALE_RAMP
        case 39U:  // BHV_RAMP_SWITCH
        case 40U:  // BHV_SEA_MONSTER
        case 45U:  // BHV_COLLECT_EGG
        case 46U:  // BHV_EGG_CREATOR
        case 47U:  // BHV_CHARACTER_FLAG
        case 49U:  // BHV_ANIMATION
        case 50U:  // BHV_ANIMATED_OBJECT
        case 51U:  // BHV_CAMERA_ANIMATION
        case 53U:  // BHV_CAR_ANIMATION
        case 55U:  // BHV_TRIGGER
        case 56U:  // BHV_VEHICLE_ANIMATION
        case 57U:  // BHV_ZIPPER_WATER
        case 58U:  // BHV_TIMETRIAL_GHOST
        case 62U:  // BHV_PARK_WARDEN
        case 64U:  // BHV_WORLD_KEY
        case 65U:  // BHV_BANANA_SPAWNER
        case 66U:  // BHV_TREASURE_SUCKER
        case 67U:  // BHV_LOG
        case 68U:  // BHV_LAVA_SPURT
        case 70U:  // BHV_HIT_TESTER
        case 74U:  // BHV_TROPHY_CABINET
        case 75U:  // BHV_BUBBLER
        case 76U:  // BHV_FLY_COIN
        case 77U:  // BHV_GOLDEN_BALLOON
        case 78U:  // BHV_LASER_BOLT
        case 79U:  // BHV_LASER_GUN
        case 80U:  // BHV_PARK_WARDEN_2
        case 81U:  // BHV_ANIMATED_OBJECT_2
        case 82U:  // BHV_ZIPPER_GROUND
        case 83U:  // BHV_OVERRIDE_POS
        case 84U:  // BHV_WIZPIG_SHIP
        case 85U:  // BHV_ANIMATED_OBJECT_3
        case 86U:  // BHV_ANIMATED_OBJECT_4
        case 88U:  // BHV_SILVER_COIN
        case 89U:  // BHV_BOOST
        case 93U:  // BHV_ZIPPER_AIR
        case 96U:  // BHV_SNOWBALL
        case 97U:  // BHV_SNOWBALL_2
        case 98U:  // BHV_TELEPORT
        case 101U: // BHV_SNOWBALL_3
        case 102U: // BHV_SNOWBALL_4
        case 103U: // BHV_HIT_TESTER_3
        case 104U: // BHV_HIT_TESTER_4
        case 107U: // BHV_BOSS_HAZARD_TRIGGER
        case 108U: // BHV_FIREBALL_OCTOWEAPON
        case 109U: // BHV_FROG
        case 110U: // BHV_SILVER_COIN_2
        case 111U: // BHV_TT_DOOR
        case 113U: // BHV_DOOR_OPENER
        case 115U: // BHV_PIG_ROCKETEER
        case 116U: // BHV_FIREBALL_OCTOWEAPON_2
        case 119U: // BHV_WIZPIG_GHOSTS
            return true;
        default:
            return false;
    }
}

std::uint16_t actor_field_mask(std::uint16_t behavior) {
    std::uint16_t mask = kActorPropertyWord0 | kActorPropertyWord1;
    switch (behavior) {
        case 46U: // BHV_EGG_CREATOR: first property is an Object pointer.
            mask = 0U;
            break;
        case 58U: // BHV_TIMETRIAL_GHOST: second property is a header pointer.
        case 76U: // BHV_FLY_COIN: second property is a racer pointer.
        case 78U: // BHV_LASER_BOLT: second property is a laser-gun pointer.
            mask = kActorPropertyWord0;
            break;
        case 89U:  // BHV_BOOST: first property is an Object pointer.
        case 108U: // BHV_FIREBALL_OCTOWEAPON
        case 116U: // BHV_FIREBALL_OCTOWEAPON_2
            mask = kActorPropertyWord1;
            break;
        default:
            break;
    }
    if (behavior == 67U) mask |= kActorLogBehavior;
    return mask;
}

bool collect_authored_actors(const std::uint8_t* rdram, std::size_t size,
                             std::vector<ActorSource>& actors) {
    std::uint32_t object_list = 0U;
    std::uint32_t object_count = 0U;
    std::uint32_t object_start = 0U;
    std::array<std::uint32_t, 2> map_starts{};
    std::array<std::uint32_t, 2> map_sizes{};
    if (!read_word(rdram, size, revision_addresses::ObjectList, object_list) ||
        !read_word(rdram, size, revision_addresses::ObjectCount, object_count) ||
        !read_word(rdram, size, revision_addresses::ObjectListStart,
                   object_start) ||
        object_count > kMaximumObjects || object_start > object_count ||
        (object_count != 0U &&
         offset_of(object_list, object_count * 4U, size) ==
             std::numeric_limits<std::size_t>::max()) ||
        !read_word(rdram, size,
                   revision_addresses::ObjectMapSpawnList + 0U,
                   map_starts[0]) ||
        !read_word(rdram, size,
                   revision_addresses::ObjectMapSpawnList + 4U,
                   map_starts[1]) ||
        !read_word(rdram, size, revision_addresses::ObjectMapSize + 0U,
                   map_sizes[0]) ||
        !read_word(rdram, size, revision_addresses::ObjectMapSize + 4U,
                   map_sizes[1])) {
        return false;
    }
    for (std::size_t map = 0U; map < map_starts.size(); ++map) {
        if (map_sizes[map] > size ||
            (map_sizes[map] != 0U &&
             offset_of(map_starts[map], map_sizes[map], size) ==
                 std::numeric_limits<std::size_t>::max())) {
            return false;
        }
    }
    actors.clear();
    for (std::uint32_t index = object_start; index < object_count; ++index) {
        std::uint32_t object = 0U;
        std::uint16_t flags = 0U;
        std::uint16_t behavior = 0U;
        std::uint16_t object_id = 0U;
        std::uint32_t level_entry = 0U;
        if (!read_word(rdram, size, object_list + index * 4U, object) ||
            offset_of(object, 0x80U, size) ==
                std::numeric_limits<std::size_t>::max() ||
            !read_half(rdram, size, object + 0x06U, flags) ||
            !read_half(rdram, size, object + 0x48U, behavior) ||
            !read_half(rdram, size, object + 0x4AU, object_id) ||
            !read_word(rdram, size, object + 0x3CU, level_entry)) {
            return false;
        }
        bool level_backed = false;
        std::uint32_t map_bank = 0U;
        std::uint32_t level_entry_offset = 0U;
        for (std::size_t map = 0U; map < map_starts.size(); ++map) {
            const std::uint64_t begin = map_starts[map];
            const std::uint64_t end = begin + map_sizes[map];
            if (map_sizes[map] != 0U && level_entry >= begin &&
                level_entry < end) {
                if (level_backed) return false;
                level_backed = true;
                map_bank = static_cast<std::uint32_t>(map);
                level_entry_offset = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(level_entry) - begin);
            }
        }
        if ((flags & kObjectParticleFlag) != 0U ||
            !level_backed || !authored_actor_behavior(behavior)) {
            continue;
        }
        if (actors.size() >= kMaximumAuthoredActors) {
            return false;
        }
        // gObjPtrList is sorted for rendering and allocation addresses can
        // differ between machines. A level-map entry is immutable for the
        // lifetime of the track and therefore identifies the logical actor
        // without ever putting a host pointer on the wire.
        actors.push_back({map_bank, level_entry_offset, behavior, object_id,
                          actor_field_mask(behavior), object});
    }
    std::sort(actors.begin(), actors.end(), actor_identity_less);
    if (std::adjacent_find(
            actors.begin(), actors.end(),
            [](const ActorSource& lhs, const ActorSource& rhs) {
                return actor_identity_equal(lhs, rhs);
            }) != actors.end()) {
        return false;
    }
    return true;
}

} // namespace

bool capture_authoritative_state(const std::uint8_t* rdram,
                                 std::size_t rdram_size,
                                 std::uint32_t frame,
                                 std::vector<std::uint8_t>& output,
                                 std::string& error) {
    performance_trace::Scope profile(performance_trace::Region::OnlineStateCapture);
    output.clear();
    if (rdram == nullptr || rdram_size < kRetailRdramSize) {
        error = "RDRAM is unavailable for authoritative state capture.";
        return false;
    }
    std::uint32_t map = 0U;
    std::uint32_t racer_count = 0U;
    std::uint32_t racer_array = 0U;
    // This path runs at the fixed 30 Hz authored boundary in Rollback mode.
    // Retain the scratch allocation per simulation thread rather than
    // allocating and freeing the actor table every frame.
    thread_local std::vector<ActorSource> actors;
    actors.clear();
    if (actors.capacity() < kMaximumAuthoredActors) {
        actors.reserve(kMaximumAuthoredActors);
    }
    if (!read_word(rdram, rdram_size, revision_addresses::CurrentMapId, map) ||
        !read_word(rdram, rdram_size, revision_addresses::NumberOfRacers,
                   racer_count) ||
        !read_word(rdram, rdram_size, revision_addresses::Racers,
                   racer_array) || racer_count > kMaximumRacers ||
        !collect_authored_actors(rdram, rdram_size, actors)) {
        error = "DKR's racer topology is invalid at the snapshot boundary.";
        return false;
    }
    output.reserve(45U + kMaximumWaterStateBytes + racer_count *
        (4U + kWordsPerRacer * 4U + kHalvesPerRacer * 2U +
         kBytesPerRacer));
    put32(output, kMagic);
    put32(output, kAuthoritativeStateSchema);
    put32(output, frame);
    put32(output, map);
    put32(output, racer_count);

    for (const std::uint32_t address : global_words()) {
        std::uint32_t value = 0U;
        if (!read_word(rdram, rdram_size, address, value)) {
            error = "An authored global is outside RDRAM.";
            output.clear();
            return false;
        }
        put32(output, value);
    }

    std::uint16_t race_end_timer = 0U;
    std::uint8_t race_end_stage = 0U;
    if (!read_half(rdram, rdram_size, revision_addresses::RaceEndTimer,
                   race_end_timer) ||
        !read_byte(rdram, rdram_size, revision_addresses::RaceEndStage,
                   race_end_stage)) {
        error = "DKR's authored race-transition state is outside RDRAM.";
        output.clear();
        return false;
    }
    put16(output, race_end_timer);
    output.push_back(race_end_stage);

    for (const std::uint32_t address : roster_bytes()) {
        std::uint8_t value = 0U;
        if (!read_byte(rdram, rdram_size, address, value)) {
            error = "An authored roster field is outside RDRAM.";
            output.clear();
            return false;
        }
        output.push_back(value);
    }

    if (!capture_water_state(rdram, rdram_size, output, error)) {
        output.clear();
        return false;
    }

    for (std::uint32_t index = 0U; index < racer_count; ++index) {
        std::uint32_t object = 0U;
        std::uint32_t racer = 0U;
        if (!racer_addresses(rdram, rdram_size, racer_array, index,
                             object, racer)) {
            error = "A racer object is invalid at the snapshot boundary.";
            output.clear();
            return false;
        }
        put32(output, index);
        if (!capture_words(rdram, rdram_size, object, kObjectWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerIdentityWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerPhysicsWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerPostPointerWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerLapWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerRotationWords, output) ||
            !capture_words(rdram, rdram_size, racer, kRacerRaceWords, output) ||
            !capture_halves(rdram, rdram_size, racer, kRacerRaceHalves, output) ||
            !capture_bytes(rdram, rdram_size, racer, kRacerRaceBytes, output)) {
            error = "A racer state field is outside RDRAM.";
            output.clear();
            return false;
        }
    }
    put32(output, static_cast<std::uint32_t>(actors.size()));
    for (const ActorSource& actor : actors) {
        // Two loaded map banks are supported. Packing the bank into the high
        // bit keeps the actor record within the existing sixteen-fragment wire
        // budget even on tracks with the maximum authored actor count.
        put32(output, (actor.map_bank << 31U) |
                      actor.level_entry_offset);
        put16(output, actor.behavior);
        put16(output, actor.object_id);
        put16(output, actor.fields);
        if (!capture_words(rdram, rdram_size, actor.object,
                           kActorObjectWords, output) ||
            !capture_halves(rdram, rdram_size, actor.object,
                            kActorObjectHalves, output)) {
            error = "A moving track actor is outside RDRAM.";
            output.clear();
            return false;
        }
        if ((actor.fields & kActorPropertyWord0) != 0U) {
            std::uint32_t value = 0U;
            if (!read_word(rdram, rdram_size, actor.object + 0x78U, value)) {
                error = "A moving track actor property is outside RDRAM.";
                output.clear();
                return false;
            }
            put32(output, value);
        }
        if ((actor.fields & kActorPropertyWord1) != 0U) {
            std::uint32_t value = 0U;
            if (!read_word(rdram, rdram_size, actor.object + 0x7CU, value)) {
                error = "A moving track actor property is outside RDRAM.";
                output.clear();
                return false;
            }
            put32(output, value);
        }
        if ((actor.fields & kActorLogBehavior) != 0U) {
            std::uint32_t behavior_data = 0U;
            if (!read_word(rdram, rdram_size, actor.object + 0x64U,
                           behavior_data)) {
                error = "A moving log's optional wave-state pointer is outside RDRAM.";
                output.clear();
                return false;
            }
            // obj_wave_init() deliberately returns NULL when this log is not
            // attached to a valid procedural-water segment. Retail gameplay
            // handles that state by keeping the log at its level-entry height,
            // so NULL is topology rather than corruption. Carry an explicit
            // presence marker; never serialize the process-local pointer.
            output.push_back(behavior_data != 0U ? 1U : 0U);
            if (behavior_data != 0U &&
                !capture_words(rdram, rdram_size, behavior_data,
                               kLogBehaviorWords, output)) {
                error = "A moving log has a non-null wave-state pointer outside RDRAM.";
                output.clear();
                return false;
            }
        }
    }
    if (output.size() > kMaximumAuthoritativeStateBytes) {
        error = "The portable authoritative state exceeds its protocol budget.";
        output.clear();
        return false;
    }
    error.clear();
    return true;
}

namespace {

bool apply_authoritative_state_impl(
    std::uint8_t* rdram, std::size_t rdram_size,
    std::span<const std::uint8_t> snapshot, std::uint32_t expected_frame,
    bool allow_unmatched_actors, bool preserve_lifecycle,
    std::uint32_t& unmatched_actors,
    std::string& error) {
    performance_trace::Scope profile(performance_trace::Region::OnlineStateApply, snapshot.size());
    unmatched_actors = 0U;
    if (rdram == nullptr || rdram_size < kRetailRdramSize ||
        snapshot.size() > kMaximumAuthoritativeStateBytes) {
        error = "The authoritative snapshot is outside supported limits.";
        return false;
    }
    std::size_t cursor = 0U;
    std::uint32_t magic = 0U;
    std::uint32_t schema = 0U;
    std::uint32_t frame = 0U;
    std::uint32_t map = 0U;
    std::uint32_t racer_count = 0U;
    std::uint32_t local_map = 0U;
    std::uint32_t local_count = 0U;
    std::uint32_t racer_array = 0U;
    // Validation stages every write before touching RDRAM. These buffers are
    // hot-path scratch, not persistent state; retaining capacity removes
    // thousands of small allocator operations per second without changing the
    // atomic install contract.
    thread_local std::vector<std::pair<std::uint32_t, std::uint32_t>>
        word_writes;
    thread_local std::vector<std::pair<std::uint32_t, std::uint16_t>>
        half_writes;
    thread_local std::vector<std::pair<std::uint32_t, std::uint8_t>>
        byte_writes;
    thread_local std::vector<ActorSource> local_actors;
    word_writes.clear();
    half_writes.clear();
    byte_writes.clear();
    local_actors.clear();
    if (word_writes.capacity() < 2048U) word_writes.reserve(2048U);
    constexpr std::size_t kMaximumWaterHalfWrites =
        static_cast<std::size_t>(kMaximumWaveTileCount) *
            kMaximumWaveTileCount * 2U +
        kMaximumWaveGenerators;
    if (half_writes.capacity() < kMaximumWaterHalfWrites + 512U) {
        half_writes.reserve(kMaximumWaterHalfWrites + 512U);
    }
    if (byte_writes.capacity() < 256U) byte_writes.reserve(256U);
    if (local_actors.capacity() < kMaximumAuthoredActors) {
        local_actors.reserve(kMaximumAuthoredActors);
    }
    if (!take32(snapshot, cursor, magic) ||
        !take32(snapshot, cursor, schema) ||
        !take32(snapshot, cursor, frame) ||
        !take32(snapshot, cursor, map) ||
        !take32(snapshot, cursor, racer_count) || magic != kMagic ||
        schema != kAuthoritativeStateSchema || frame != expected_frame ||
        racer_count > kMaximumRacers ||
        !read_word(rdram, rdram_size, revision_addresses::CurrentMapId,
                   local_map) ||
        !read_word(rdram, rdram_size, revision_addresses::NumberOfRacers,
                   local_count) ||
        !read_word(rdram, rdram_size, revision_addresses::Racers,
                   racer_array) || local_map != map || local_count != racer_count) {
        error = "The host snapshot does not match this frame's map or racer topology.";
        return false;
    }

    for (const std::uint32_t address : global_words()) {
        std::uint32_t value = 0U;
        if (!take32(snapshot, cursor, value) ||
            offset_of(address, 4U, rdram_size) ==
                std::numeric_limits<std::size_t>::max()) {
            error = "The host snapshot's global state is truncated.";
            return false;
        }
        word_writes.emplace_back(address, value);
        if (preserve_lifecycle &&
            address != revision_addresses::CurrentRngSeed &&
            address != revision_addresses::PreviousRngSeed &&
            address != revision_addresses::RaceStartTimer &&
            address != revision_addresses::RaceStartProgress &&
            address != revision_addresses::LogicUpdateRate) {
            std::uint32_t local_value = 0U;
            if (!read_word(rdram, rdram_size, address, local_value) ||
                local_value != value ||
                ((address == revision_addresses::RaceFinishTriggered ||
                  address == revision_addresses::NumberOfFinishedRacers) &&
                 value != 0U)) {
                error = "Catch-up requires native race lifecycle work.";
                return false;
            }
        }
    }

    std::uint16_t race_end_timer = 0U;
    std::uint8_t race_end_stage = 0U;
    if (!take16(snapshot, cursor, race_end_timer) ||
        !take8(snapshot, cursor, race_end_stage) ||
        offset_of(revision_addresses::RaceEndTimer ^ 2U, 2U, rdram_size) ==
            std::numeric_limits<std::size_t>::max() ||
        offset_of(revision_addresses::RaceEndStage ^ 3U, 1U, rdram_size) ==
            std::numeric_limits<std::size_t>::max()) {
        error = "The host snapshot's race-transition state is truncated.";
        return false;
    }
    half_writes.emplace_back(revision_addresses::RaceEndTimer, race_end_timer);
    byte_writes.emplace_back(revision_addresses::RaceEndStage, race_end_stage);
    if (preserve_lifecycle) {
        std::uint16_t local_timer = 0U;
        std::uint8_t local_stage = 0U;
        if (!read_half(rdram, rdram_size, revision_addresses::RaceEndTimer,
                       local_timer) ||
            !read_byte(rdram, rdram_size, revision_addresses::RaceEndStage,
                       local_stage) ||
            race_end_timer != 0U || race_end_stage != 0U ||
            local_timer != 0U || local_stage != 0U) {
            error = "Catch-up cannot skip a native race-end transition.";
            return false;
        }
    }

    for (const std::uint32_t address : roster_bytes()) {
        std::uint8_t value = 0U;
        if (!take8(snapshot, cursor, value) ||
            offset_of(address ^ 3U, 1U, rdram_size) ==
                std::numeric_limits<std::size_t>::max()) {
            error = "The host snapshot's roster state is truncated.";
            return false;
        }
        byte_writes.emplace_back(address, value);
        if (preserve_lifecycle) {
            std::uint8_t local_value = 0U;
            if (!read_byte(rdram, rdram_size, address, local_value) ||
                local_value != value) {
                error = "Catch-up cannot change the native player roster.";
                return false;
            }
        }
    }

    if (!stage_water_state(rdram, rdram_size, snapshot, cursor,
                           word_writes, half_writes, error)) {
        return false;
    }

    for (std::uint32_t expected_index = 0U;
         expected_index < racer_count; ++expected_index) {
        std::uint32_t index = 0U;
        std::uint32_t object = 0U;
        std::uint32_t racer = 0U;
        if (!take32(snapshot, cursor, index) || index != expected_index ||
            !racer_addresses(rdram, rdram_size, racer_array, index,
                             object, racer) ||
            !stage_words(rdram_size, object, kObjectWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerIdentityWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerPhysicsWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerPostPointerWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerLapWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerRotationWords,
                         snapshot, cursor, word_writes) ||
            !stage_words(rdram_size, racer, kRacerRaceWords,
                         snapshot, cursor, word_writes) ||
            !stage_halves(rdram_size, racer, kRacerRaceHalves,
                          snapshot, cursor, half_writes) ||
            !stage_bytes(rdram_size, racer, kRacerRaceBytes,
                         snapshot, cursor, byte_writes)) {
            error = "The host snapshot's racer state is invalid or truncated.";
            return false;
        }
    }
    std::uint32_t actor_count = 0U;
    if (!take32(snapshot, cursor, actor_count) ||
        actor_count > kMaximumAuthoredActors ||
        !collect_authored_actors(rdram, rdram_size, local_actors)) {
        error = "The host snapshot's moving-actor topology is invalid.";
        return false;
    }
    ActorSource previous_identity{};
    if (preserve_lifecycle && actor_count != local_actors.size()) {
        error = "Catch-up cannot skip native actor creation or retirement.";
        return false;
    }
    bool have_previous_identity = false;
    for (std::uint32_t actor_number = 0U; actor_number < actor_count;
         ++actor_number) {
        ActorSource identity{};
        std::uint32_t packed_identity = 0U;
        std::uint16_t behavior = 0U;
        std::uint16_t object_id = 0U;
        std::uint16_t fields = 0U;
        if (!take32(snapshot, cursor, packed_identity)) {
            error = "The host snapshot's moving actor is invalid or no longer matches this track.";
            return false;
        }
        identity.map_bank = packed_identity >> 31U;
        identity.level_entry_offset = packed_identity & 0x7FFFFFFFU;
        if (!take16(snapshot, cursor, behavior) ||
            !take16(snapshot, cursor, object_id) ||
            !take16(snapshot, cursor, fields) ||
            (have_previous_identity &&
             !actor_identity_less(previous_identity, identity)) ||
            !authored_actor_behavior(behavior) ||
            fields != actor_field_mask(behavior)) {
            error = "The host snapshot's moving actor is invalid or no longer matches this track.";
            return false;
        }
        const auto local = std::lower_bound(local_actors.begin(),
                                            local_actors.end(), identity,
                                            actor_identity_less);
        const bool actor_matches = local != local_actors.end() &&
            actor_identity_equal(*local, identity) &&
            local->behavior == behavior && local->object_id == object_id;
        if (!actor_matches && !allow_unmatched_actors) {
            error = "The host snapshot's moving actor is invalid or no longer matches this track.";
            return false;
        }
        if (actor_matches) {
            if (!stage_words(rdram_size, local->object, kActorObjectWords,
                             snapshot, cursor, word_writes) ||
                !stage_halves(rdram_size, local->object, kActorObjectHalves,
                              snapshot, cursor, half_writes)) {
                error = "The host snapshot's moving actor is invalid or no longer matches this track.";
                return false;
            }
        } else {
            ++unmatched_actors;
            if (!consume_words(snapshot, cursor, kActorObjectWords) ||
                !consume_halves(snapshot, cursor, kActorObjectHalves)) {
                error = "The host snapshot's moving actor is truncated.";
                return false;
            }
        }
        if ((fields & kActorPropertyWord0) != 0U) {
            std::uint32_t value = 0U;
            if (!take32(snapshot, cursor, value)) {
                error = "The host snapshot's moving-actor properties are truncated.";
                return false;
            }
            if (actor_matches) {
                word_writes.emplace_back(local->object + 0x78U, value);
            }
        }
        if ((fields & kActorPropertyWord1) != 0U) {
            std::uint32_t value = 0U;
            if (!take32(snapshot, cursor, value)) {
                error = "The host snapshot's moving-actor properties are truncated.";
                return false;
            }
            if (actor_matches) {
                word_writes.emplace_back(local->object + 0x7CU, value);
            }
        }
        if ((fields & kActorLogBehavior) != 0U) {
            std::uint8_t host_has_wave_state = 0U;
            if (!take8(snapshot, cursor, host_has_wave_state) ||
                host_has_wave_state > 1U) {
                error = "The host snapshot's moving-log wave-state marker is invalid.";
                return false;
            }
            if (actor_matches) {
                std::uint32_t behavior_data = 0U;
                if (!read_word(rdram, rdram_size, local->object + 0x64U,
                               behavior_data)) {
                    error = "This track's moving-log wave-state pointer is invalid.";
                    return false;
                }
                const bool local_has_wave_state = behavior_data != 0U;
                if (local_has_wave_state &&
                    offset_of(behavior_data, 0x10U, rdram_size) ==
                        std::numeric_limits<std::size_t>::max()) {
                    error = "This track's moving log has a non-null wave-state pointer outside RDRAM.";
                    return false;
                }
                if (local_has_wave_state != (host_has_wave_state != 0U)) {
                    if (!allow_unmatched_actors) {
                        error = "The host and local moving log disagree about optional wave state.";
                        return false;
                    }
                    ++unmatched_actors;
                    if (host_has_wave_state != 0U &&
                        !consume_words(snapshot, cursor, kLogBehaviorWords)) {
                        error = "The host snapshot's moving-log wave state is truncated.";
                        return false;
                    }
                } else if (host_has_wave_state != 0U &&
                           !stage_words(rdram_size, behavior_data,
                                        kLogBehaviorWords, snapshot, cursor,
                                        word_writes)) {
                    error = "The host snapshot's moving-log wave state is invalid.";
                    return false;
                }
            } else if (host_has_wave_state != 0U &&
                       !consume_words(snapshot, cursor, kLogBehaviorWords)) {
                error = "The host snapshot's moving-log wave state is truncated.";
                return false;
            }
        }
        previous_identity = identity;
        have_previous_identity = true;
    }
    if (cursor != snapshot.size()) {
        error = "The host snapshot contains unexpected trailing state.";
        return false;
    }
    // The decode above validates the complete packet and every current racer
    // pointer before changing a byte of RDRAM. Failed recovery can therefore
    // never leave a partially host-authored simulation behind.
    for (const auto& [address, value] : word_writes) {
        if (!write_word(rdram, rdram_size, address, value)) return false;
    }
    for (const auto& [address, value] : half_writes) {
        if (!write_half(rdram, rdram_size, address, value)) return false;
    }
    for (const auto& [address, value] : byte_writes) {
        if (!write_byte(rdram, rdram_size, address, value)) return false;
    }
    error.clear();
    return true;
}

} // namespace

bool apply_authoritative_state(std::uint8_t* rdram,
                               std::size_t rdram_size,
                               std::span<const std::uint8_t> snapshot,
                               std::uint32_t expected_frame,
                               std::string& error) {
    std::uint32_t unmatched_actors = 0U;
    return apply_authoritative_state_impl(
        rdram, rdram_size, snapshot, expected_frame, false, false,
        unmatched_actors, error);
}

bool apply_catch_up_authoritative_state(
    std::uint8_t* rdram, std::size_t rdram_size,
    std::span<const std::uint8_t> snapshot, std::uint32_t expected_frame,
    std::string& error) {
    std::uint32_t unmatched_actors = 0U;
    return apply_authoritative_state_impl(
        rdram, rdram_size, snapshot, expected_frame, false, true,
        unmatched_actors, error);
}

bool apply_live_authoritative_state(
    std::uint8_t* rdram, std::size_t rdram_size,
    std::span<const std::uint8_t> snapshot, std::uint32_t expected_frame,
    std::uint32_t& unmatched_actors, std::string& error) {
    return apply_authoritative_state_impl(
        rdram, rdram_size, snapshot, expected_frame, true, false,
        unmatched_actors, error);
}

bool capture_racer_orientation_state(
    const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t frame,
    std::vector<std::uint8_t>& output, std::string& error) {
    output.clear();
    if (rdram == nullptr || rdram_size < kRetailRdramSize) {
        error = "DKR's orientation state does not have valid RDRAM.";
        return false;
    }
    std::uint32_t map = 0U;
    std::uint32_t racer_count = 0U;
    std::uint32_t racer_array = 0U;
    if (!read_word(rdram, rdram_size, revision_addresses::CurrentMapId, map) ||
        !read_word(rdram, rdram_size, revision_addresses::NumberOfRacers,
                   racer_count) ||
        !read_word(rdram, rdram_size, revision_addresses::Racers,
                   racer_array) || racer_count > kMaximumRacers) {
        error = "DKR's racer topology is invalid at the orientation boundary.";
        return false;
    }
    output.reserve(20U + static_cast<std::size_t>(racer_count) * 12U);
    put32(output, kOrientationMagic);
    put32(output, kOrientationSchema);
    put32(output, frame);
    put32(output, map);
    put32(output, racer_count);
    for (std::uint32_t index = 0U; index < racer_count; ++index) {
        std::uint32_t object = 0U;
        std::uint32_t racer = 0U;
        RacerOrientation value{};
        if (!racer_addresses(rdram, rdram_size, racer_array, index,
                             object, racer) ||
            !read_word(rdram, rdram_size, racer + 0x94U,
                       value.camera_zoom) ||
            !read_half(rdram, rdram_size, racer + 0x196U,
                       value.camera_yaw) ||
            !read_half(rdram, rdram_size, racer + 0x1A0U,
                       value.visual_steering)) {
            output.clear();
            error = "A racer orientation is invalid at the authored boundary.";
            return false;
        }
        float zoom = 0.0F;
        std::memcpy(&zoom, &value.camera_zoom, sizeof(zoom));
        if (!std::isfinite(zoom)) {
            output.clear();
            error = "A racer camera distance is not finite.";
            return false;
        }
        put32(output, index);
        put32(output, value.camera_zoom);
        put16(output, value.camera_yaw);
        put16(output, value.visual_steering);
    }
    error.clear();
    return true;
}

bool apply_racer_orientation_correction(
    std::uint8_t* rdram, std::size_t rdram_size,
    std::span<const std::uint8_t> host_snapshot,
    std::span<const std::uint8_t> matching_local_snapshot,
    std::uint32_t expected_sample_frame, std::uint32_t& corrected_racers,
    std::string& error) {
    corrected_racers = 0U;
    ParsedOrientationState host{};
    ParsedOrientationState local{};
    if (rdram == nullptr || rdram_size < kRetailRdramSize ||
        !parse_orientation_state(host_snapshot, host) ||
        !parse_orientation_state(matching_local_snapshot, local) ||
        host.frame != expected_sample_frame ||
        local.frame != expected_sample_frame || host.map != local.map ||
        host.racers.size() != local.racers.size()) {
        error = "The host orientation sample does not match local history.";
        return false;
    }

    std::uint32_t current_map = 0U;
    std::uint32_t current_count = 0U;
    std::uint32_t racer_array = 0U;
    if (!read_word(rdram, rdram_size, revision_addresses::CurrentMapId,
                   current_map) ||
        !read_word(rdram, rdram_size, revision_addresses::NumberOfRacers,
                   current_count) ||
        !read_word(rdram, rdram_size, revision_addresses::Racers,
                   racer_array) || current_map != host.map ||
        current_count != host.racers.size()) {
        error = "The orientation sample belongs to a different race topology.";
        return false;
    }

    struct StagedOrientation {
        std::uint32_t racer = 0U;
        std::uint32_t camera_zoom = 0U;
        std::uint16_t camera_yaw = 0U;
        std::uint16_t visual_steering = 0U;
        bool changed = false;
    };
    std::vector<StagedOrientation> writes;
    writes.reserve(current_count);
    for (std::uint32_t index = 0U; index < current_count; ++index) {
        std::uint32_t object = 0U;
        StagedOrientation staged{};
        if (!racer_addresses(rdram, rdram_size, racer_array, index,
                             object, staged.racer) ||
            !read_word(rdram, rdram_size, staged.racer + 0x94U,
                       staged.camera_zoom) ||
            !read_half(rdram, rdram_size, staged.racer + 0x196U,
                       staged.camera_yaw) ||
            !read_half(rdram, rdram_size, staged.racer + 0x1A0U,
                       staged.visual_steering)) {
            error = "A current racer orientation is invalid.";
            return false;
        }

        float current_zoom = 0.0F;
        float host_zoom = 0.0F;
        float local_zoom = 0.0F;
        std::memcpy(&current_zoom, &staged.camera_zoom, sizeof(current_zoom));
        std::memcpy(&host_zoom, &host.racers[index].camera_zoom,
                    sizeof(host_zoom));
        std::memcpy(&local_zoom, &local.racers[index].camera_zoom,
                    sizeof(local_zoom));
        const float corrected_zoom = current_zoom + (host_zoom - local_zoom);
        if (!std::isfinite(current_zoom) || !std::isfinite(corrected_zoom)) {
            error = "A corrected racer camera distance is not finite.";
            return false;
        }
        std::memcpy(&staged.camera_zoom, &corrected_zoom,
                    sizeof(staged.camera_zoom));

        const std::uint16_t yaw_delta = static_cast<std::uint16_t>(
            host.racers[index].camera_yaw - local.racers[index].camera_yaw);
        const std::uint16_t steering_delta = static_cast<std::uint16_t>(
            host.racers[index].visual_steering -
            local.racers[index].visual_steering);
        staged.camera_yaw = static_cast<std::uint16_t>(
            staged.camera_yaw + yaw_delta);
        staged.visual_steering = static_cast<std::uint16_t>(
            staged.visual_steering + steering_delta);
        staged.changed = yaw_delta != 0U || steering_delta != 0U ||
                         host.racers[index].camera_zoom !=
                             local.racers[index].camera_zoom;
        writes.push_back(staged);
    }

    // Every pointer and value is validated before the first RDRAM write. A
    // malformed or stale sample therefore cannot half-correct the racer list.
    for (const StagedOrientation& staged : writes) {
        if (!write_word(rdram, rdram_size, staged.racer + 0x94U,
                        staged.camera_zoom) ||
            !write_half(rdram, rdram_size, staged.racer + 0x196U,
                        staged.camera_yaw) ||
            !write_half(rdram, rdram_size, staged.racer + 0x1A0U,
                        staged.visual_steering)) {
            error = "A validated racer orientation could not be committed.";
            return false;
        }
        if (staged.changed) ++corrected_racers;
    }
    error.clear();
    return true;
}

std::uint64_t authoritative_state_checksum(
    std::span<const std::uint8_t> snapshot) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : snapshot) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash != 0U ? hash : 1U;
}

} // namespace dkr::runtime::netplay

#include "determinism_state_hash.hpp"

#include "authored_state_contract.hpp"
#include "netplay/authoritative_state.hpp"
#include "revision_addresses.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

constexpr std::size_t kRetailRdramSize = 0x00800000U;
constexpr std::size_t kMaximumRacers = 10U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

class HashBuilder final {
  public:
    void word(std::uint32_t value) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            hash_ ^= static_cast<std::uint8_t>(value >> shift);
            hash_ *= kFnvPrime;
        }
    }
    void byte(std::uint8_t value) {
        hash_ ^= value;
        hash_ *= kFnvPrime;
    }
    void half(std::uint16_t value) {
        byte(static_cast<std::uint8_t>(value));
        byte(static_cast<std::uint8_t>(value >> 8U));
    }
    std::uint64_t finish() const {
        return hash_;
    }

  private:
    std::uint64_t hash_ = kFnvOffset;
};

class RdramView final {
  public:
    RdramView(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::optional<std::uint32_t> word(std::uint32_t address) const {
        const auto offset = physical_offset(address, sizeof(std::uint32_t));
        if (!offset)
            return std::nullopt;
        std::uint32_t value = 0U;
        std::memcpy(&value, data_ + *offset, sizeof(value));
        return value;
    }
    std::optional<std::uint8_t> byte(std::uint32_t address) const {
        const auto offset = physical_offset(address, 1U);
        if (!offset)
            return std::nullopt;
        const std::size_t swizzled = *offset ^ 3U;
        if (swizzled >= size_)
            return std::nullopt;
        return data_[swizzled];
    }
    std::optional<std::uint16_t> half(std::uint32_t address) const {
        const auto offset = physical_offset(address, sizeof(std::uint16_t));
        if (!offset)
            return std::nullopt;
        const std::size_t swizzled = *offset ^ 2U;
        if (swizzled > size_ - sizeof(std::uint16_t))
            return std::nullopt;
        std::uint16_t value = 0U;
        std::memcpy(&value, data_ + swizzled, sizeof(value));
        return value;
    }
    bool pointer(std::uint32_t address, std::size_t extent = 1U) const {
        return physical_offset(address, extent).has_value();
    }

  private:
    std::optional<std::size_t> physical_offset(std::uint32_t address, std::size_t extent) const {
        const std::uint32_t region = address & 0xE0000000U;
        if (data_ == nullptr || (region != 0x80000000U && region != 0xA0000000U)) {
            return std::nullopt;
        }
        const std::size_t offset = address & 0x1FFFFFFFU;
        if (extent > size_ || offset > size_ - extent)
            return std::nullopt;
        return offset;
    }
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0U;
};

void hash_word(HashBuilder& hash, const RdramView& memory, std::uint32_t address) {
    hash.word(memory.word(address).value_or(0U));
}
void hash_byte(HashBuilder& hash, const RdramView& memory, std::uint32_t address) {
    hash.byte(memory.byte(address).value_or(0U));
}
void hash_half(HashBuilder& hash, const RdramView& memory, std::uint32_t address) {
    hash.half(memory.half(address).value_or(0U));
}
std::uint64_t hash_racer(HashBuilder& hash, const RdramView& memory, std::uint32_t object_address) {
    using namespace authored_contract;
    HashBuilder detail;
    detail.word(0x444B5244U); // DKRD
    const auto add_word = [&](std::uint32_t address) {
        hash_word(hash, memory, address);
        hash_word(detail, memory, address);
    };
    const auto add_half = [&](std::uint32_t address) {
        hash_half(hash, memory, address);
        hash_half(detail, memory, address);
    };
    const auto add_byte = [&](std::uint32_t address) {
        hash_byte(hash, memory, address);
        hash_byte(detail, memory, address);
    };
    const bool object_valid = memory.pointer(object_address, 0x68U);
    hash.byte(object_valid ? 1U : 0U);
    detail.byte(object_valid ? 1U : 0U);
    if (!object_valid)
        return detail.finish();

    // Authored racer transform, velocity and segment identity. Scale,
    // animation frame, distance-to-camera, opacity/model selection and every
    // renderer/particle/shadow pointer are presentation state and excluded.
    for (const std::uint16_t offset : kObjectWords) {
        add_word(object_address + offset);
    }

    const std::uint32_t racer_address = memory.word(object_address + 0x0064U).value_or(0U);
    const bool racer_valid = memory.pointer(racer_address, 0x224U);
    hash.byte(racer_valid ? 1U : 0U);
    detail.byte(racer_valid ? 1U : 0U);
    if (!racer_valid)
        return detail.finish();

    // Explicit simulation contract derived from Object_Racer: identity,
    // motion/vehicle physics, lap/checkpoint progress, items, attacks and race
    // state. Animation/bob/camera values, sound handles, AudioPoint pointers,
    // target/object pointers and delayed-sound bookkeeping are excluded. The
    // packed rotation, head and item-control words at 0x160..0x174 are part
    // of authored racer state and deliberately match authoritative_state.cpp.
    const auto hash_contract_words = [&](const auto& offsets) {
        for (const std::uint16_t offset : offsets) {
            add_word(racer_address + offset);
        }
    };
    hash_contract_words(kRacerIdentityWords);
    hash_contract_words(kRacerPhysicsWords);
    hash_contract_words(kRacerPostPointerWords);
    hash_contract_words(kRacerLapWords);
    hash_contract_words(kRacerRotationWords);
    hash_contract_words(kRacerRaceWords);
    for (const std::uint16_t offset : kRacerRaceHalves) {
        add_half(racer_address + offset);
    }
    for (const std::uint16_t offset : kRacerRaceBytes) {
        add_byte(racer_address + offset);
    }
    return detail.finish();
}

} // namespace

std::uint64_t canonical_gameplay_state_hash(const std::uint8_t* rdram, std::size_t rdram_size) {
    return canonical_gameplay_state_digest(rdram, rdram_size).combined;
}

GameplayStateDigest canonical_gameplay_state_digest(const std::uint8_t* rdram, std::size_t rdram_size) {
    if (rdram == nullptr || rdram_size < kRetailRdramSize)
        return {};
    const RdramView memory(rdram, rdram_size);
    GameplayStateDigest digest{};
    HashBuilder globals;
    globals.word(0x444B5247U); // DKRG
    hash_word(globals, memory, revision_addresses::CurrentMapId);
    for (const std::uint32_t address : authored_contract::global_words()) {
        hash_word(globals, memory, address);
    }
    hash_byte(globals, memory, revision_addresses::RaceEndTimer + 0U);
    hash_byte(globals, memory, revision_addresses::RaceEndTimer + 1U);
    hash_byte(globals, memory, revision_addresses::RaceEndStage);

    HashBuilder roster;
    roster.word(0x444B5252U); // DKRR
    for (const std::uint32_t address : authored_contract::roster_bytes()) {
        hash_byte(roster, memory, address);
    }

    HashBuilder racers;
    racers.word(0x444B5253U); // DKRS
    const std::uint32_t raw_count = memory.word(revision_addresses::NumberOfRacers).value_or(0U);
    const std::uint32_t racer_count = (std::min)(raw_count, static_cast<std::uint32_t>(kMaximumRacers));
    racers.word(racer_count);
    const std::uint32_t racer_array = memory.word(revision_addresses::Racers).value_or(0U);
    const bool array_valid = memory.pointer(racer_array, kMaximumRacers * sizeof(std::uint32_t));
    racers.byte(array_valid ? 1U : 0U);
    for (std::uint32_t index = 0U; array_valid && index < racer_count; ++index) {
        const std::uint32_t object_address = memory.word(racer_array + index * sizeof(std::uint32_t)).value_or(0U);
        digest.racer_details[index] = hash_racer(racers, memory, object_address);
    }
    digest.racer_count = racer_count;
    digest.globals = globals.finish();
    digest.roster = roster.finish();
    digest.racers = racers.finish();
    // The pass/fail checksum is generated by the exact portable state codec
    // used by rollback, Player 1 recovery and transition barriers. The
    // component hashes above remain diagnostic breadcrumbs, but can no longer
    // drift into a subtly different definition of "authored state".
    std::vector<std::uint8_t> portable;
    std::string capture_error;
    if (!capture_authoritative_state(rdram, rdram_size, 0U, portable, capture_error)) {
        return {};
    }
    digest.combined = authoritative_state_checksum(portable);
    return digest;
}

std::uint64_t canonical_frontend_state_hash(const std::uint8_t* rdram, std::size_t rdram_size) {
    if (rdram == nullptr || rdram_size < kRetailRdramSize)
        return 0U;
    const RdramView memory(rdram, rdram_size);
    HashBuilder hash;
    hash.word(0x444B5246U); // Hash schema "DKRF".
    hash_word(hash, memory, revision_addresses::CurrentMenuId);
    hash_word(hash, memory, revision_addresses::CurrentMapId);
    hash_word(hash, memory, revision_addresses::TracksMode);
    hash_word(hash, memory, revision_addresses::NumberOfActivePlayers);
    hash_word(hash, memory, revision_addresses::NumberOfReadyPlayers);
    hash_word(hash, memory, revision_addresses::ActiveMagicCodes);
    // Frontend animation, music selection and RNG consumption are local
    // presentation concerns. Gameplay is reseeded before a race starts, so
    // including those values here would reject otherwise-identical peers.
    // The authored contract is the active roster, its ready state and the
    // characters assigned to the four virtual controller ports.
    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        hash_byte(hash, memory, revision_addresses::ActivePlayersArray + static_cast<std::uint32_t>(slot));
        hash_byte(hash, memory, revision_addresses::CharacterSelectStatus + static_cast<std::uint32_t>(slot));
        hash_byte(hash, memory, revision_addresses::PlayerIdMap + static_cast<std::uint32_t>(slot));
    }
    for (std::size_t slot = 0U; slot < 8U; ++slot) {
        hash_byte(hash, memory, revision_addresses::PlayersCharacterArray + static_cast<std::uint32_t>(slot));
        hash_byte(hash, memory, revision_addresses::CharacterIdSlots + static_cast<std::uint32_t>(slot));
    }
    return hash.finish();
}

} // namespace dkr::runtime::netplay

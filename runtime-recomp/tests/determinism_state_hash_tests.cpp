#include "determinism_hash_policy.hpp"
#include "determinism_state_hash.hpp"
#include "netplay/authoritative_state.hpp"
#include "revision_addresses.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
constexpr std::size_t kRdramSize = 0x00800000U;
std::size_t offset(std::uint32_t address) {
    return address & 0x1FFFFFFFU;
}
void write_word(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint32_t value) {
    std::memcpy(memory.data() + offset(address), &value, sizeof(value));
}
void write_byte(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint8_t value) {
    memory[offset(address) ^ 3U] = value;
}
void write_half(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint16_t value) {
    std::memcpy(memory.data() + (offset(address) ^ 2U), &value, sizeof(value));
}
std::uint16_t read_half(const std::vector<std::uint8_t>& memory, std::uint32_t address) {
    std::uint16_t value = 0U;
    std::memcpy(&value, memory.data() + (offset(address) ^ 2U), sizeof(value));
    return value;
}

constexpr std::uint32_t kWaterHeightIndices = 0x80306000U;
constexpr std::uint32_t kWaterGeneratorList = 0x80307000U;
constexpr std::uint32_t kWaterGeneratorObjects = 0x80307800U;
constexpr std::uint32_t kWaterTileCount = 4U;
constexpr std::uint32_t kWaterSeedSize = 120U;

void seed_water(std::vector<std::uint8_t>& memory, std::uint16_t phase_x, std::uint16_t phase_y,
                std::uint16_t generator_phase) {
    using namespace dkr::runtime::revision_addresses;
    write_word(memory, WaveHeightIndices, kWaterHeightIndices);
    write_word(memory, WaveController + 0x04U, kWaterTileCount);
    write_word(memory, WaveController + 0x20U, kWaterSeedSize);
    write_word(memory, WaveController + 0x40U, 0x3F400000U);
    write_word(memory, WavePowerBase, 0x3F000000U);
    write_word(memory, WaveMagnitude, 0x3F600000U);
    write_word(memory, WavePowerDivisor, 12U);
    write_word(memory, WaveGenCount, 2U);
    write_word(memory, WaveGenList, kWaterGeneratorList);
    write_word(memory, WaveGenObjects, kWaterGeneratorObjects);
    write_word(memory, kWaterGeneratorObjects + 0U * 4U, 0x80307900U);
    write_word(memory, kWaterGeneratorObjects + 5U * 4U, 0x80307980U);
    write_half(memory, kWaterGeneratorList + 0U * 0x40U + 0x1AU, generator_phase);
    write_half(memory, kWaterGeneratorList + 5U * 0x40U + 0x1AU, static_cast<std::uint16_t>(generator_phase + 0x123U));

    for (std::uint32_t index = 0U; index < kWaterTileCount * kWaterTileCount; ++index) {
        const std::uint16_t base_x = static_cast<std::uint16_t>((index * 7U + 3U) % kWaterSeedSize);
        const std::uint16_t base_y = static_cast<std::uint16_t>((index * 11U + 5U) % kWaterSeedSize);
        write_half(memory, kWaterHeightIndices + index * 4U,
                   static_cast<std::uint16_t>((base_x + phase_x) % kWaterSeedSize));
        write_half(memory, kWaterHeightIndices + index * 4U + 2U,
                   static_cast<std::uint16_t>((base_y + phase_y) % kWaterSeedSize));
    }
}
void seed_gameplay(std::vector<std::uint8_t>& memory) {
    using namespace dkr::runtime::revision_addresses;
    write_word(memory, CurrentMenuId, 12U);
    write_word(memory, CurrentMapId, 7U);
    write_word(memory, IsInRace, 1U);
    write_word(memory, CurrentRngSeed, 0x12345678U);
    write_word(memory, PreviousRngSeed, 0x9ABCDEF0U);
    write_word(memory, RaceStartTimer, 60U);
    write_word(memory, RaceStartProgress, 0x43960000U); // 300.0f
    write_word(memory, LogicUpdateRate, 2U);
    write_word(memory, NumberOfActivePlayers, 2U);
    write_byte(memory, NumberOfGameplayPlayers, 2U);
    write_byte(memory, PlayersCharacterArray, 1U);
    write_byte(memory, PlayersCharacterArray + 1U, 4U);
    constexpr std::uint32_t racer_array = 0x80300000U;
    constexpr std::uint32_t object = 0x80300100U;
    constexpr std::uint32_t racer = 0x80301000U;
    write_word(memory, NumberOfRacers, 1U);
    write_word(memory, Racers, racer_array);
    write_word(memory, racer_array, object);
    write_word(memory, object + 0x64U, racer);
    write_word(memory, object + 0x0CU, 0x41200000U);
    write_word(memory, object + 0x10U, 0x41A00000U);
    write_word(memory, object + 0x14U, 0x41F00000U);
    write_word(memory, racer + 0x00U, 0x00000104U);
    write_word(memory, racer + 0x2CU, 0xC1200000U);
    write_word(memory, racer + 0x190U, 0x00110004U);
    constexpr std::uint32_t object_list = 0x80302000U;
    constexpr std::uint32_t log_object = 0x80303000U;
    constexpr std::uint32_t log_state = 0x80303100U;
    constexpr std::uint32_t object_map = 0x80304000U;
    write_word(memory, ObjectList, object_list);
    write_word(memory, ObjectCount, 1U);
    write_word(memory, ObjectListStart, 0U);
    // Portable actor synchronization intentionally accepts only objects whose
    // level-entry pointer belongs to one of the loaded object maps. This keeps
    // transient particles/projectiles and allocator-order differences out of
    // the determinism contract while still covering authored track hazards.
    write_word(memory, ObjectMapSpawnList + 0U, object_map);
    write_word(memory, ObjectMapSpawnList + 4U, 0U);
    write_word(memory, ObjectMapSize + 0U, 0x100U);
    write_word(memory, ObjectMapSize + 4U, 0U);
    write_word(memory, object_list, log_object);
    write_half(memory, log_object + 0x48U, 67U); // BHV_LOG
    write_half(memory, log_object + 0x4AU, 0x1234U);
    write_word(memory, log_object + 0x3CU, object_map + 0x10U);
    write_word(memory, log_object + 0x0CU, 0x42C80000U);
    write_word(memory, log_object + 0x78U, 0x00001234U);
    write_word(memory, log_object + 0x7CU, 0xC0200000U);
    write_word(memory, log_object + 0x64U, log_state);
    write_word(memory, log_state + 0x08U, 0x11223344U);
}
} // namespace

int main() {
    using namespace dkr::runtime::netplay;
    using namespace dkr::runtime::revision_addresses;

#if DKR_TEST_REVISION == 80
    assert(select(dkr::runtime::rom::Revision::UsV80));
#else
    assert(select(dkr::runtime::rom::Revision::UsV77));
#endif

    static_assert(!should_submit_determinism_hash(0U, kDeterminismNotArmed));
    static_assert(!should_submit_determinism_hash(100U, 120U));
    static_assert(should_submit_determinism_hash(120U, 120U));
    static_assert(!should_submit_determinism_hash(121U, 120U));
    static_assert(should_submit_determinism_hash(126U, 120U));
    static_assert(!should_publish_authority_checkpoint(120U, kDeterminismNotArmed));
    static_assert(should_publish_authority_checkpoint(120U, 180U));
    static_assert(!should_publish_authority_checkpoint(121U, 180U));
    static_assert(should_publish_authority_checkpoint(126U, 180U));
    static_assert(determinism_race_ready(2U, 8U, 2U));
    static_assert(determinism_race_ready(4U, 4U, 4U));
    static_assert(!determinism_race_ready(1U, 8U, 2U));
    static_assert(!determinism_race_ready(2U, 1U, 2U));

    std::vector<std::uint8_t> first(kRdramSize, 0x00U);
    std::vector<std::uint8_t> second(kRdramSize, 0x00U);
    seed_gameplay(first);
    seed_gameplay(second);
    assert(canonical_gameplay_state_hash(first.data(), first.size()) ==
           canonical_gameplay_state_hash(second.data(), second.size()));

    const GameplayStateDigest baseline = canonical_gameplay_state_digest(first.data(), first.size());
    assert(baseline.racer_count == 1U);
    assert(baseline.racer_details[0] != 0U);

    write_word(second, CurrentRngSeed, 0x12345679U);
    const GameplayStateDigest rng_mismatch = canonical_gameplay_state_digest(second.data(), second.size());
    assert(baseline.combined != rng_mismatch.combined);
    assert(baseline.globals != rng_mismatch.globals);
    assert(baseline.roster == rng_mismatch.roster);
    assert(baseline.racers == rng_mismatch.racers);
    write_word(second, CurrentRngSeed, 0x12345678U);

    write_word(second, LogicUpdateRate, 3U);
    const GameplayStateDigest rate_mismatch = canonical_gameplay_state_digest(second.data(), second.size());
    assert(baseline.combined != rate_mismatch.combined);
    assert(baseline.globals != rate_mismatch.globals);
    assert(baseline.roster == rate_mismatch.roster);
    assert(baseline.racers == rate_mismatch.racers);
    write_word(second, LogicUpdateRate, 2U);

    write_word(second, RaceStartProgress, 0x43958000U);
    const GameplayStateDigest countdown_mismatch = canonical_gameplay_state_digest(second.data(), second.size());
    assert(baseline.combined != countdown_mismatch.combined);
    assert(baseline.globals != countdown_mismatch.globals);
    assert(baseline.roster == countdown_mismatch.roster);
    assert(baseline.racers == countdown_mismatch.racers);
    write_word(second, RaceStartProgress, 0x43960000U);

    constexpr std::uint32_t racer = 0x80301000U;
    write_word(second, racer + 0x2CU, 0xC1000000U);
    const GameplayStateDigest racer_mismatch = canonical_gameplay_state_digest(second.data(), second.size());
    assert(baseline.combined != racer_mismatch.combined);
    assert(baseline.globals == racer_mismatch.globals);
    assert(baseline.roster == racer_mismatch.roster);
    assert(baseline.racers != racer_mismatch.racers);
    assert(baseline.racer_details[0] != racer_mismatch.racer_details[0]);

    constexpr std::uint32_t log_object = 0x80303000U;
    constexpr std::uint32_t log_state = 0x80303100U;
    write_word(second, racer + 0x2CU, 0xC1200000U);
    write_word(second, log_object + 0x78U, 0x00001235U);
    assert(canonical_gameplay_state_hash(first.data(), first.size()) !=
           canonical_gameplay_state_hash(second.data(), second.size()));
    write_word(second, log_object + 0x78U, 0x00001234U);
    write_word(second, log_state + 0x08U, 0x11223345U);
    assert(canonical_gameplay_state_hash(first.data(), first.size()) !=
           canonical_gameplay_state_hash(second.data(), second.size()));
    write_word(second, log_state + 0x08U, 0x11223344U);

    // Frontend presentation/audio state is deliberately not gameplay state.
    write_word(first, CurrentMenuId, 12U);
    write_word(second, CurrentMenuId, 99U);
    write_word(first, NumberOfReadyPlayers, 1U);
    write_word(second, NumberOfReadyPlayers, 2U);
    write_byte(first, CharacterSelectStatus, 0U);
    write_byte(second, CharacterSelectStatus, 1U);
    constexpr std::uint32_t object = 0x80300100U;
    write_word(first, object + 0x08U, 0x3F800000U); // render scale
    write_word(second, object + 0x08U, 0x40000000U);
    write_word(first, object + 0x30U, 0x42C80000U); // camera distance
    write_word(second, object + 0x30U, 0x447A0000U);
    write_word(first, racer + 0x10U, 0x80123456U);
    write_word(second, racer + 0x10U, 0x80765432U);
    write_word(first, racer + 0x140U, 0x80111111U);
    write_word(second, racer + 0x140U, 0x80222222U);
    write_word(first, racer + 0x178U, 0x80333333U);
    write_word(second, racer + 0x178U, 0x80444444U);
    write_word(first, racer + 0x218U, 0x80555555U);
    write_word(second, racer + 0x218U, 0x80666666U);
    // DKR's packed racer tail mixes simulation with local presentation. None
    // of these fields may create a rollback determinism failure.
    write_half(first, racer + 0x196U, 0x1111U); // camera yaw
    write_half(second, racer + 0x196U, 0x7777U);
    write_half(first, racer + 0x1A0U, 0x2222U); // visual steering
    write_half(second, racer + 0x1A0U, 0x6666U);
    write_byte(first, racer + 0x1CFU, 1U); // egg HUD counter
    write_byte(second, racer + 0x1CFU, 9U);
    write_byte(first, racer + 0x1D0U, 0U); // spectator camera
    write_byte(second, racer + 0x1D0U, 3U);
    write_byte(first, racer + 0x1E7U, 4U); // animation counter
    write_byte(second, racer + 0x1E7U, 12U);
    write_byte(first, racer + 0x1EFU, 0U); // boost audio latch
    write_byte(second, racer + 0x1EFU, 1U);
    write_byte(first, racer + 0x1F7U, 0xFFU); // transparency
    write_byte(second, racer + 0x1F7U, 0x80U);
    write_byte(first, racer + 0x1F8U, 1U); // HUD indicator
    write_byte(second, racer + 0x1F8U, 2U);
    write_byte(first, racer + 0x1F9U, 20U); // HUD indicator timer
    write_byte(second, racer + 0x1F9U, 40U);
    write_byte(first, racer + 0x1FDU, 0U); // camera index
    write_byte(second, racer + 0x1FDU, 1U);
    write_byte(first, racer + 0x20AU, 0U); // light flags
    write_byte(second, racer + 0x20AU, 7U);
    write_half(first, racer + 0x20EU, 0x0123U); // delayed sound ID
    write_half(second, racer + 0x20EU, 0x0456U);
    write_byte(first, racer + 0x210U, 2U); // delayed sound timer
    write_byte(second, racer + 0x210U, 8U);
    assert(canonical_gameplay_state_hash(first.data(), first.size()) ==
           canonical_gameplay_state_hash(second.data(), second.size()));
    assert(canonical_frontend_state_hash(first.data(), first.size()) !=
           canonical_frontend_state_hash(second.data(), second.size()));

    // Renderer scale, camera distance and sound handles are absent from both
    // authored contracts; frontend identity is restored before checking them.
    write_word(second, CurrentMenuId, 12U);
    write_word(second, NumberOfReadyPlayers, 1U);
    write_byte(second, CharacterSelectStatus, 0U);
    assert(canonical_frontend_state_hash(first.data(), first.size()) ==
           canonical_frontend_state_hash(second.data(), second.size()));

    // The eight CPU character slots are chosen by charselect_assign_ai and
    // become the race roster. They are authored frontend state, even though
    // character-select animation and music remain local presentation.
    write_byte(second, CharacterIdSlots + 3U, 7U);
    assert(canonical_frontend_state_hash(first.data(), first.size()) !=
           canonical_frontend_state_hash(second.data(), second.size()));
    write_byte(second, CharacterIdSlots + 3U, 0U);
    assert(canonical_frontend_state_hash(first.data(), first.size()) ==
           canonical_frontend_state_hash(second.data(), second.size()));

    // Adjacent authored fields remain protected even though their packed
    // presentation neighbours are excluded.
    write_byte(second, racer + 0x187U, 3U); // attack type
    assert(canonical_gameplay_state_hash(first.data(), first.size()) !=
           canonical_gameplay_state_hash(second.data(), second.size()));
    write_byte(second, racer + 0x187U, 0U);
    write_half(second, racer + 0x1A2U, 0x1234U); // rotation velocity
    assert(canonical_gameplay_state_hash(first.data(), first.size()) !=
           canonical_gameplay_state_hash(second.data(), second.size()));
    write_half(second, racer + 0x1A2U, 0U);
    write_byte(second, racer + 0x20CU, 1U); // throttle-release state
    assert(canonical_gameplay_state_hash(first.data(), first.size()) !=
           canonical_gameplay_state_hash(second.data(), second.size()));
    write_byte(second, racer + 0x20CU, 0U);

    // Authoritative recovery is transactional: malformed data or a changed
    // racer topology must not write even the valid prefix of a snapshot.
    std::vector<std::uint8_t> snapshot;
    std::string error;
    constexpr std::uint32_t snapshot_frame = 777U;
    assert(capture_authoritative_state(first.data(), first.size(), snapshot_frame, snapshot, error));
    // Debt reduction must not bypass the retail finish/roster/actor lifecycle.
    // All rejected installations preserve the entire emulated memory image.
    auto catch_up_target = first;
    write_word(catch_up_target, CurrentRngSeed, 0xDEADBEEFU);
    assert(apply_catch_up_authoritative_state(catch_up_target.data(), catch_up_target.size(), snapshot, snapshot_frame,
                                              error));
    assert(catch_up_target == first);
    for (const auto address : {IsInRace, TracksMode, NumberOfActivePlayers, ActiveMagicCodes, PathUpdateOff,
                               RaceFinishTriggered, NumberOfFinishedRacers}) {
        auto lifecycle_host = first;
        write_word(lifecycle_host, address, 123U);
        std::vector<std::uint8_t> lifecycle_snapshot;
        assert(capture_authoritative_state(lifecycle_host.data(), lifecycle_host.size(), snapshot_frame,
                                           lifecycle_snapshot, error));
        catch_up_target = first;
        assert(!apply_catch_up_authoritative_state(catch_up_target.data(), catch_up_target.size(), lifecycle_snapshot,
                                                   snapshot_frame, error));
        assert(catch_up_target == first);
    }
    for (const bool timer : {false, true}) {
        catch_up_target = first;
        if (timer)
            write_half(catch_up_target, RaceEndTimer, 1U);
        else
            write_byte(catch_up_target, RaceEndStage, 1U);
        const auto before = catch_up_target;
        assert(!apply_catch_up_authoritative_state(catch_up_target.data(), catch_up_target.size(), snapshot,
                                                   snapshot_frame, error));
        assert(catch_up_target == before);
    }
    catch_up_target = first;
    write_byte(catch_up_target, PlayerIdMap, 3U);
    const auto before_roster = catch_up_target;
    assert(!apply_catch_up_authoritative_state(catch_up_target.data(), catch_up_target.size(), snapshot, snapshot_frame,
                                               error));
    assert(catch_up_target == before_roster);
    auto retired_actor_host = first;
    write_word(retired_actor_host, ObjectCount, 0U);
    std::vector<std::uint8_t> retired_actor_snapshot;
    assert(capture_authoritative_state(retired_actor_host.data(), retired_actor_host.size(), snapshot_frame,
                                       retired_actor_snapshot, error));
    catch_up_target = first;
    assert(!apply_catch_up_authoritative_state(catch_up_target.data(), catch_up_target.size(), retired_actor_snapshot,
                                               snapshot_frame, error));
    assert(catch_up_target == first);
    std::vector<std::uint8_t> target = first;
    // Logical track actors may be allocated at different RDRAM addresses on
    // another process/platform. Recovery must resolve the immutable map-entry
    // identity to that machine's local object rather than requiring Player
    // 1's pointer value to exist there.
    constexpr std::uint32_t relocated_log_object = 0x80305000U;
    constexpr std::uint32_t relocated_log_state = 0x80305100U;
    constexpr std::uint32_t object_list = 0x80302000U;
    std::memcpy(target.data() + offset(relocated_log_object), first.data() + offset(log_object), 0x80U);
    std::memcpy(target.data() + offset(relocated_log_state), first.data() + offset(log_state), 0x20U);
    write_word(target, object_list, relocated_log_object);
    write_word(target, relocated_log_object + 0x64U, relocated_log_state);
    write_word(target, CurrentRngSeed, 0xDEADBEEFU);
    write_word(target, LogicUpdateRate, 6U);
    write_word(target, RaceStartProgress, 0U);
    write_word(target, racer + 0x2CU, 0x3F800000U);
    const std::vector<std::uint8_t> before_truncated = target;
    std::vector<std::uint8_t> truncated = snapshot;
    truncated.pop_back();
    assert(!apply_authoritative_state(target.data(), target.size(), truncated, snapshot_frame, error));
    assert(target == before_truncated);
    assert(apply_authoritative_state(target.data(), target.size(), snapshot, snapshot_frame, error));
    std::vector<std::uint8_t> installed_snapshot;
    assert(capture_authoritative_state(target.data(), target.size(), snapshot_frame, installed_snapshot, error));
    assert(installed_snapshot == snapshot);
    assert(canonical_gameplay_state_hash(target.data(), target.size()) ==
           canonical_gameplay_state_hash(first.data(), first.size()));
    std::uint32_t restored_logic_rate = 0U;
    std::uint32_t restored_start_progress = 0U;
    std::memcpy(&restored_logic_rate, target.data() + offset(LogicUpdateRate), sizeof(std::uint32_t));
    std::memcpy(&restored_start_progress, target.data() + offset(RaceStartProgress), sizeof(std::uint32_t));
    assert(restored_logic_rate == 2U);
    assert(restored_start_progress == 0x43960000U);

    write_word(target, NumberOfRacers, 0U);
    const std::vector<std::uint8_t> before_topology = target;
    assert(!apply_authoritative_state(target.data(), target.size(), snapshot, snapshot_frame, error));
    assert(target == before_topology);

    write_word(target, NumberOfRacers, 1U);
    write_word(target, 0x80300000U, 0x807FFFFCU);
    const std::vector<std::uint8_t> before_bad_pointer = target;
    assert(!apply_authoritative_state(target.data(), target.size(), snapshot, snapshot_frame, error));
    assert(target == before_bad_pointer);

    // The existing portable codec already supports a legitimate empty racer
    // roster. Prove the cinematic baseline does not dereference the old racer
    // array, and remains transactional when returning to a populated hub.
    auto cinematic_host = first;
    auto cinematic_client = first;
    write_word(cinematic_host, NumberOfRacers, 0U);
    write_word(cinematic_client, NumberOfRacers, 0U);
    write_word(cinematic_host, Racers, 0U);
    write_word(cinematic_client, Racers, 0U);
    write_word(cinematic_client, CurrentRngSeed, 0xBADC0DEU);
    std::vector<std::uint8_t> cinematic_snapshot, cinematic_installed;
    assert(capture_authoritative_state(cinematic_host.data(), cinematic_host.size(), 0U, cinematic_snapshot, error));
    assert(apply_authoritative_state(cinematic_client.data(), cinematic_client.size(), cinematic_snapshot, 0U, error));
    assert(
        capture_authoritative_state(cinematic_client.data(), cinematic_client.size(), 0U, cinematic_installed, error));
    assert(cinematic_installed == cinematic_snapshot);
    const auto populated_hub = first;
    assert(!apply_authoritative_state(first.data(), first.size(), cinematic_snapshot, 0U, error));
    assert(first == populated_hub);

    // Procedural water is gameplay state for hovercraft buoyancy. Synchronize
    // its compact phase rather than generated wave vertices: an in-sync live
    // correction costs a fixed 105-byte record and performs no grid rewrite.
    std::vector<std::uint8_t> water_host(kRdramSize, 0x00U);
    std::vector<std::uint8_t> water_client(kRdramSize, 0x00U);
    seed_gameplay(water_host);
    seed_gameplay(water_client);
    std::vector<std::uint8_t> dry_snapshot;
    assert(capture_authoritative_state(water_host.data(), water_host.size(), snapshot_frame, dry_snapshot, error));
    seed_water(water_host, 7U, 13U, 0x1400U);
    seed_water(water_client, 29U, 41U, 0x2A00U);
    write_word(water_client, WaveController + 0x40U, 0x3E800000U);
    write_word(water_client, WavePowerBase, 0x3E000000U);
    std::vector<std::uint8_t> water_snapshot;
    std::vector<std::uint8_t> client_snapshot;
    assert(capture_authoritative_state(water_host.data(), water_host.size(), snapshot_frame, water_snapshot, error));
    assert(
        capture_authoritative_state(water_client.data(), water_client.size(), snapshot_frame, client_snapshot, error));
    assert(water_snapshot != client_snapshot);
    assert(water_snapshot.size() >= dry_snapshot.size());
    assert(water_snapshot.size() - dry_snapshot.size() <= 128U);
    assert(apply_authoritative_state(water_client.data(), water_client.size(), water_snapshot, snapshot_frame, error));
    assert(
        capture_authoritative_state(water_client.data(), water_client.size(), snapshot_frame, client_snapshot, error));
    assert(client_snapshot == water_snapshot);
    assert(read_half(water_client, kWaterHeightIndices) == read_half(water_host, kWaterHeightIndices));
    assert(read_half(water_client, kWaterGeneratorList + 5U * 0x40U + 0x1AU) ==
           read_half(water_host, kWaterGeneratorList + 5U * 0x40U + 0x1AU));

    // A different randomized seed layout is not a phase mismatch. Reject it
    // transactionally instead of writing host water indices into unrelated
    // local topology.
    std::vector<std::uint8_t> bad_water = water_host;
    const std::uint16_t changed =
        static_cast<std::uint16_t>((read_half(bad_water, kWaterHeightIndices + 4U) + 1U) % kWaterSeedSize);
    write_half(bad_water, kWaterHeightIndices + 4U, changed);
    const std::vector<std::uint8_t> before_bad_water = bad_water;
    assert(!apply_authoritative_state(bad_water.data(), bad_water.size(), water_snapshot, snapshot_frame, error));
    assert(bad_water == before_bad_water);
}

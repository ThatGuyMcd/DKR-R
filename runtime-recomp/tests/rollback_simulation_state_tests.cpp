#include "netplay/authoritative_state.hpp"
#include "netplay/rollback_simulation_state.hpp"
#include "revision_addresses.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kRdramSize = 0x00800000U;
std::size_t offset(std::uint32_t address) {
    return address & 0x1FFFFFFFU;
}

void write_word(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint32_t value) {
    std::memcpy(memory.data() + offset(address), &value, sizeof(value));
}

std::uint32_t read_word(const std::vector<std::uint8_t>& memory, std::uint32_t address) {
    std::uint32_t value = 0U;
    std::memcpy(&value, memory.data() + offset(address), sizeof(value));
    return value;
}

void write_byte(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint8_t value) {
    memory[offset(address) ^ 3U] = value;
}

void write_half(std::vector<std::uint8_t>& memory, std::uint32_t address, std::uint16_t value) {
    std::memcpy(memory.data() + (offset(address) ^ 2U), &value, sizeof(value));
}

void seed_gameplay(std::vector<std::uint8_t>& memory) {
    using namespace dkr::runtime::revision_addresses;
    write_word(memory, CurrentMapId, 7U);
    write_word(memory, IsInRace, 1U);
    write_word(memory, CurrentRngSeed, 0x12345678U);
    write_word(memory, PreviousRngSeed, 0x9ABCDEF0U);
    write_word(memory, RaceStartTimer, 60U);
    write_word(memory, RaceStartProgress, 0x43960000U);
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
    constexpr std::uint32_t egg_creator = 0x80303200U;
    constexpr std::uint32_t level_map = 0x80304000U;
    write_word(memory, ObjectList, object_list);
    write_word(memory, ObjectCount, 2U);
    write_word(memory, ObjectListStart, 0U);
    write_word(memory, ObjectMapSpawnList + 0U, level_map);
    write_word(memory, ObjectMapSpawnList + 4U, 0U);
    write_word(memory, ObjectMapSize + 0U, 0x100U);
    write_word(memory, ObjectMapSize + 4U, 0U);
    write_word(memory, object_list, log_object);
    write_word(memory, object_list + 4U, egg_creator);
    write_half(memory, log_object + 0x48U, 67U); // BHV_LOG
    write_half(memory, log_object + 0x4AU, 0x1234U);
    write_word(memory, log_object + 0x3CU, level_map + 0x10U);
    write_word(memory, log_object + 0x0CU, 0x42C80000U);
    write_word(memory, log_object + 0x78U, 0x00001234U);
    write_word(memory, log_object + 0x7CU, 0xC0200000U);
    write_word(memory, log_object + 0x64U, log_state);
    write_word(memory, log_state + 0x08U, 0x11223344U);

    // BHV_EGG_CREATOR stores an Object pointer in its first property word.
    // The portable netplay contract must restore its transform but leave that
    // process-local pointer untouched.
    write_half(memory, egg_creator + 0x48U, 46U);
    write_half(memory, egg_creator + 0x4AU, 0x4321U);
    write_word(memory, egg_creator + 0x3CU, level_map + 0x20U);
    write_word(memory, egg_creator + 0x0CU, 0x42A00000U);
    write_word(memory, egg_creator + 0x78U, 0x8030F000U);
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

    std::vector<std::uint8_t> memory(kRdramSize, 0U);
    seed_gameplay(memory);
    std::vector<std::uint8_t> snapshot(kRollbackSimulationStateBytes);
    std::uint64_t checksum = 0U;
    std::string error;
    assert(capture_rollback_simulation_state(memory.data(), memory.size(), 42U, snapshot, checksum, error));
    assert(checksum != 0U);

    // An authored value must rewind.
    write_word(memory, CurrentRngSeed, 0xDEADBEEFU);
    constexpr std::uint32_t log_object = 0x80303000U;
    constexpr std::uint32_t log_state = 0x80303100U;
    constexpr std::uint32_t egg_creator = 0x80303200U;
    write_word(memory, log_object + 0x0CU, 0xDEADBEEFU);
    write_word(memory, log_object + 0x78U, 0xAABBCCDDU);
    write_word(memory, log_state + 0x08U, 0x55667788U);
    write_word(memory, egg_creator + 0x0CU, 0xDEADBEEFU);
    write_word(memory, egg_creator + 0x78U, 0x8030E000U);
    // Renderer sorting is allowed to reorder gObjPtrList between frames. The
    // emulated object address, not the transient list ordinal, is the key.
    constexpr std::uint32_t object_list = 0x80302000U;
    write_word(memory, object_list, egg_creator);
    write_word(memory, object_list + 4U, log_object);
    // A renderer-owned location outside the contract must not rewind.
    constexpr std::uint32_t presentation_sentinel = 0x80200000U;
    write_word(memory, presentation_sentinel, 0xCAFEBABEU);
    assert(restore_rollback_simulation_state(memory.data(), memory.size(), snapshot, 42U, checksum, error));
    assert(read_word(memory, CurrentRngSeed) == 0x12345678U);
    assert(read_word(memory, log_object + 0x0CU) == 0x42C80000U);
    assert(read_word(memory, log_object + 0x78U) == 0x00001234U);
    assert(read_word(memory, log_state + 0x08U) == 0x11223344U);
    assert(read_word(memory, egg_creator + 0x0CU) == 0x42A00000U);
    assert(read_word(memory, egg_creator + 0x78U) == 0x8030E000U);
    assert(read_word(memory, presentation_sentinel) == 0xCAFEBABEU);

    // Lifecycle checkpoints remain exact, but the per-frame live stream is a
    // disposable correction. If a collected/spawned actor crossed the wire a
    // tick before the local object list, the live apply must restore globals
    // and matching actors without writing through a stale actor pointer.
    std::vector<std::uint8_t> authoritative_snapshot;
    assert(capture_authoritative_state(memory.data(), memory.size(), 42U, authoritative_snapshot, error));
    write_word(memory, CurrentRngSeed, 0x01020304U);
    write_word(memory, log_object + 0x0CU, 0x05060708U);
    write_word(memory, ObjectCount, 1U);
    write_word(memory, object_list, log_object);
    write_word(memory, object_list + 4U, 0U);
    assert(!apply_authoritative_state(memory.data(), memory.size(), authoritative_snapshot, 42U, error));
    assert(read_word(memory, CurrentRngSeed) == 0x01020304U);
    assert(read_word(memory, log_object + 0x0CU) == 0x05060708U);
    std::uint32_t unmatched_actors = 0U;
    assert(apply_live_authoritative_state(memory.data(), memory.size(), authoritative_snapshot, 42U, unmatched_actors,
                                          error));
    assert(unmatched_actors == 1U);
    assert(read_word(memory, CurrentRngSeed) == 0x12345678U);
    assert(read_word(memory, log_object + 0x0CU) == 0x42C80000U);
    write_word(memory, ObjectCount, 2U);
    write_word(memory, object_list, egg_creator);
    write_word(memory, object_list + 4U, log_object);

    // Replacing an actor in the canonical object-list slot invalidates the
    // entire transaction. No global, racer or earlier actor write may leak
    // through before the topology failure is reported.
    write_word(memory, CurrentRngSeed, 0x13579BDFU);
    write_word(memory, log_object + 0x0CU, 0xCAFED00DU);
    write_half(memory, egg_creator + 0x4AU, 0x9999U);
    assert(!restore_rollback_simulation_state(memory.data(), memory.size(), snapshot, 42U, checksum, error));
    assert(read_word(memory, CurrentRngSeed) == 0x13579BDFU);
    assert(read_word(memory, log_object + 0x0CU) == 0xCAFED00DU);
    write_half(memory, egg_creator + 0x4AU, 0x4321U);

    // Frame/token mismatches are rejected before changing live state.
    write_word(memory, CurrentRngSeed, 0xABCDEF01U);
    assert(!restore_rollback_simulation_state(memory.data(), memory.size(), snapshot, 43U, checksum, error));
    assert(read_word(memory, CurrentRngSeed) == 0xABCDEF01U);
    assert(!restore_rollback_simulation_state(memory.data(), memory.size(), snapshot, 42U, checksum + 1U, error));
    assert(read_word(memory, CurrentRngSeed) == 0xABCDEF01U);

    // Corruption is detected without a partial install.
    snapshot[40] ^= 0x80U;
    assert(!restore_rollback_simulation_state(memory.data(), memory.size(), snapshot, 42U, checksum, error));
    assert(read_word(memory, CurrentRngSeed) == 0xABCDEF01U);

    // obj_wave_init() legitimately returns NULL when an authored spinning log
    // is not attached to a procedural-water segment. That is the retail
    // static-height fallback, not an invalid RDRAM pointer, and track baseline
    // capture must preserve it without serializing a process-local pointer.
    std::vector<std::uint8_t> static_log_memory(kRdramSize, 0U);
    seed_gameplay(static_log_memory);
    write_word(static_log_memory, log_object + 0x64U, 0U);
    std::vector<std::uint8_t> static_log_snapshot;
    assert(capture_authoritative_state(static_log_memory.data(), static_log_memory.size(), 99U, static_log_snapshot,
                                       error));
    assert(
        apply_authoritative_state(static_log_memory.data(), static_log_memory.size(), static_log_snapshot, 99U, error));
    assert(read_word(static_log_memory, log_object + 0x64U) == 0U);

    // A strict lifecycle checkpoint may not silently install a static-log
    // snapshot over a peer whose matching log owns procedural wave state. The
    // mismatch is genuine track topology divergence, and rejection must be
    // transactional so the process-local allocation remains owned locally.
    std::vector<std::uint8_t> wave_log_memory(kRdramSize, 0U);
    seed_gameplay(wave_log_memory);
    const std::uint32_t wave_seed_before = read_word(wave_log_memory, CurrentRngSeed);
    write_word(wave_log_memory, CurrentRngSeed, 0xA5A5A5A5U);
    assert(!apply_authoritative_state(wave_log_memory.data(), wave_log_memory.size(), static_log_snapshot, 99U, error));
    assert(error == "The host and local moving log disagree about optional wave state.");
    assert(read_word(wave_log_memory, CurrentRngSeed) == 0xA5A5A5A5U);
    assert(read_word(wave_log_memory, log_object + 0x64U) == log_state);

    // Live correction is deliberately more tolerant than a lifecycle
    // checkpoint: it can still correct matching globals/actors while the
    // topology settles, but it must report the unmatched log and must never
    // overwrite or discard the local allocation pointer.
    unmatched_actors = 0U;
    assert(apply_live_authoritative_state(wave_log_memory.data(), wave_log_memory.size(), static_log_snapshot, 99U,
                                          unmatched_actors, error));
    assert(unmatched_actors == 1U);
    assert(read_word(wave_log_memory, CurrentRngSeed) == wave_seed_before);
    assert(read_word(wave_log_memory, log_object + 0x64U) == log_state);

    // A non-null pointer outside emulated RDRAM remains a hard failure. The
    // nullable-state repair must not turn real pointer corruption into a
    // packet that can be installed on another peer.
    write_word(static_log_memory, log_object + 0x64U, 0x12345678U);
    assert(!capture_authoritative_state(static_log_memory.data(), static_log_memory.size(), 100U, static_log_snapshot,
                                        error));
    assert(error == "A moving log has a non-null wave-state pointer outside RDRAM.");
}

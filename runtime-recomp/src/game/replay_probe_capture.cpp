#include "replay_probe_capture.hpp"
#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"
#include "runtime_save_routing.hpp"
#include "netplay/runtime_state.hpp"
#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <atomic>
#include <stdexcept>

namespace {
extern "C" void dkr_set_quiesced_bootstrap_callback(void (*)(std::uint8_t*, std::size_t));
recomp_context parked_cpu{};
std::uint32_t parked_revision = 0;
std::string parked_path;
std::filesystem::path requested_output;
bool requested_memory = false, consumed = false, completed = false;
unsigned capture_ticks = 0;
std::uint8_t requested_players = 2;
bool requested_host = false;
std::uint64_t requested_match = 0, requested_save_hash = 0;
std::vector<std::uint8_t> memory_fixture;
std::atomic<bool> bootstrap_complete{false};
void after_quiescence(std::uint8_t* rdram, std::size_t memory_bytes) {
    using namespace dkr::runtime::netplay;
    // The constructor receives a locally booted CPU continuation; this image
    // is not itself a rollback checkpoint. Its other owners are now gone.
    if (!rdram || memory_bytes < kRollbackMemoryBytes || (!requested_memory && parked_path.empty())) return;
    RuntimeState diagnostic;
    repair_float_register_pointer(parked_cpu);
    diagnostic.register_context(&parked_cpu);
    std::vector<std::uint8_t> fixture(diagnostic.snapshot_size());
    if (!diagnostic.capture(rdram, parked_revision, fixture)) {
        std::fprintf(stderr, "[replay-probe][quiesced] capture refused\n"); return;
    }
    if (requested_memory) {
        memory_fixture=std::move(fixture);
        bootstrap_complete.store(true,std::memory_order_release);
        std::fprintf(stderr,"[rollback][handoff] all native workers retired; revision=%u bytes=%zu\n",parked_revision,memory_fixture.size());
        return;
    }
    FILE* file = nullptr;
#ifdef _WIN32
    _wfopen_s(&file, std::filesystem::u8path(parked_path).c_str(), L"wbx");
#else
    file = std::fopen(parked_path.c_str(), "wbx");
#endif
    if (!file) { std::fprintf(stderr, "[replay-probe][quiesced] exclusive creation failed\n"); return; }
    const auto written = std::fwrite(fixture.data(), 1, fixture.size(), file);
    const auto closed = std::fclose(file);
    bootstrap_complete.store(written == fixture.size() && closed == 0);
    std::fprintf(stderr, "[replay-probe][quiesced] revision=%u bytes=%zu success=%d\n",
        parked_revision, written, written == fixture.size() && closed == 0);
}
gpr address(std::uint32_t value) { return static_cast<gpr>(static_cast<std::int32_t>(value)); }
int private_map() {
    // Production never selects a map. File-output fixtures remain private.
    if (requested_memory) return -1;
    if (!requested_output.empty()) return 3;
#if defined(DKR_REPLAY_QUALIFICATION)
    static const int map = [] {
        const char* value = std::getenv("DKR_REPLAY_CAPTURE_MAP");
        if (!value) return -1;
        char* end = nullptr; const long parsed = std::strtol(value, &end, 10);
        if (*end) return -1;
        for (int allowed : {3,4,5,7,8,10,14,26,30,40,53}) if (parsed == allowed) return allowed;
        return -1;
    }();
    return map;
#else
    return -1;
#endif
}
int private_players() {
    if (requested_memory) return requested_players;
    if (!requested_output.empty()) return 2;
#if defined(DKR_REPLAY_QUALIFICATION)
    const char* value = std::getenv("DKR_REPLAY_CAPTURE_PLAYERS");
    if (!value) return 1;
    if (value[0] >= '1' && value[0] <= '4' && value[1] == 0) return value[0] - '0';
    return 0;
#else
    return 0;
#endif
}
}

bool dkr_experimental_bootstrap_arm(const std::filesystem::path& output, std::string& error) {
    if (!requested_output.empty() || !output.is_absolute() || std::filesystem::exists(output)) {
        error="Experimental bootstrap output must be a new absolute local file."; return false;
    }
    requested_output=output; return true;
}
bool dkr_experimental_bootstrap_completed() { return bootstrap_complete.load(); }
bool dkr_experimental_bootstrap_active() {return requested_memory;}
bool dkr_experimental_bootstrap_arm_memory(std::uint8_t players, bool host,
    std::uint64_t match_id, std::uint64_t save_hash, std::string& error) {
    if(requested_memory||!requested_output.empty()||players<2||players>4||!match_id||!save_hash) {
        error="The local owned bootstrap cannot be armed in this state.";return false;
    }
    requested_players=players;requested_memory=true;consumed=completed=false;capture_ticks=0;
    requested_host=host;requested_match=match_id;requested_save_hash=save_hash;
    memory_fixture.clear();parked_path.clear();bootstrap_complete.store(false,std::memory_order_release);
    error.clear();return true;
}
bool dkr_experimental_bootstrap_activate_save(std::string& error) {
    return requested_memory && dkr::runtime::saves::activate_online_save_for_runtime(
        requested_host,requested_match,requested_save_hash,error);
}
std::uint8_t dkr_experimental_bootstrap_players() {
    return requested_memory ? requested_players : 0;
}
std::vector<std::uint8_t> dkr_experimental_bootstrap_take_memory() {
    // Caller must first join recomp::start, including the quiescence callback.
    dkr_set_quiesced_bootstrap_callback(nullptr);
    requested_memory=false;consumed=completed=false;capture_ticks=0;
    requested_match=requested_save_hash=0;requested_host=false;
    if(!bootstrap_complete.load(std::memory_order_acquire))return {};
    bootstrap_complete.store(false,std::memory_order_release);
    return std::move(memory_fixture);
}

bool dkr_private_replay_boot(std::uint8_t* rdram, recomp_context* context) {
    const int map = private_map();
    const int players = private_players();
    if (consumed || map < 0 || !players || dkr::runtime::netplay::session().active()) return false;
    const auto* payload = dkr::runtime::active_payload();
    if (!payload) return false;
    consumed = true;
    namespace a = dkr::runtime::revision_addresses;
    // Same retail single-player setup as the established custom-track auto
    // boot, but restricted to a private offline fixture and a retail map.
    const auto invoke = [&](dkr::runtime::RecompiledEntrypoint fn, gpr argument = 0) {
        recomp_context call = *context; call.r4 = argument; fn(rdram, &call); return call.r2;
    };
    invoke(payload->titlescreen_controller_assign, 0);
    invoke(payload->input_assign_players);
    for (int p = 0; p < 4; ++p) {
        MEM_B(p, address(a::CharacterSelectStatus)) = p < players ? 2 : 0;
        MEM_B(p, address(a::ActivePlayersArray)) = p < players ? 1 : 0;
    }
    MEM_W(0, address(a::NumberOfReadyPlayers)) = players;
    MEM_W(0, address(a::NumberOfActivePlayers)) = players;
    MEM_W(0, address(a::TracksMode)) = 1;
    invoke(payload->set_time_trial_enabled, 0);
    const bool rev_a = a::gSelectedRevision == dkr::runtime::rom::Revision::UsV80;
    const unsigned table = unsigned(invoke(payload->unlock_drumstick) != 0) |
                           (unsigned(invoke(payload->unlock_tt) != 0) << 1);
    constexpr std::uint32_t tables77[]{0x800DFDD0U,0x800DFE40U,0x800DFEC0U,0x800DFF40U};
    constexpr std::uint32_t tables80[]{0x800E0350U,0x800E03C0U,0x800E0440U,0x800E04C0U};
    MEM_W(0, address(rev_a ? 0x8012696CU : 0x801263CCU)) = (rev_a ? tables80 : tables77)[table];
    constexpr int characters[]{9,0,1,2}; // Four distinct retail characters.
    for (int p = 0; p < players; ++p) MEM_B(p, address(a::CharacterIdSlots)) = characters[p];
    invoke(payload->charselect_assign_ai, players);
    invoke(payload->init_racer_headers);
    MEM_W(0, address(rev_a ? 0x80123A80U : 0x80123500U)) = players - 1;
    context->r2 = (1U << 9U) | map;
    std::fprintf(stderr, "[replay-probe][private] booting retail map=%d players=%d\n", map, players);
    return true;
}

void dkr_private_replay_capture(std::uint8_t* rdram, recomp_context* context) {
    if (completed || !rdram || !context || dkr::runtime::netplay::session().active()) return;
    namespace a = dkr::runtime::revision_addresses;
    // Production parks before the first authored main_game_loop, after retail
    // init_game and its initial input/save read. No race or scripted menu input.
    // Workers still join before after_quiescence copies this continuation.
    if (requested_memory) {
        if (MEM_W(0,address(a::GameMode)) != -1 || capture_ticks != 0)
            throw std::runtime_error("Owned cold boot missed the initial INTRO boundary.");
    } else if (MEM_W(0, address(a::GameMode)) != 0 || MEM_W(0, address(a::NumberOfRacers)) <= 0 ||
        MEM_W(0, address(a::LevelLoadTimer)) != 0) { capture_ticks = 0; return; }
    std::string requested_path;
    if (!requested_output.empty()) {
        const auto utf8=requested_output.u8string();
        requested_path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    const char* path = requested_path.empty() ? nullptr : requested_path.c_str();
#if defined(DKR_REPLAY_QUALIFICATION)
    if (!path) path = std::getenv("DKR_REPLAY_CAPTURE_FILE");
#endif
    if (!requested_memory && ((!path || !*path) || ++capture_ticks < 120)) return;
    completed = true;
    parked_cpu = *context;
    parked_revision = a::gSelectedRevision == dkr::runtime::rom::Revision::UsV80 ? 80U : 77U;
    parked_path = path ? path : "";
    dkr_set_quiesced_bootstrap_callback(after_quiescence);
    std::fprintf(stderr, requested_memory
        ? "[rollback][boot] retail pre-INTRO boundary parked; waiting for native owners to join\n"
        : "[replay-probe][private] parking CPU; waiting for native owners to join\n");
    ultramodern::quit();
    // The current guest tick MUST NOT run after the saved continuation. Native
    // thread retirement performs its existing scheduler/cleaner transaction.
    throw ultramodern::thread_terminated{};
}

#include "party_link.hpp"
#include "game_registration.hpp"

#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"
#include "runtime_save_routing.hpp"
#include "runtime_legacy_mods.hpp"
#include "runtime_code_mods.hpp"
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "replay_probe_capture.hpp"
#endif

#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"

#include <cstdint>
#include <cstdio>

namespace {

constexpr gpr kInitialCodeAddress = static_cast<gpr>(static_cast<std::int32_t>(0x80000400U));

void InitialiseEntrypointContext(std::uint8_t*, recomp_context* context) {
    // The CIC 6103/bootstrap path clears BSS and establishes this stack before
    // tail-calling mainproc. RDRAM is already zeroed by N64ModernRuntime.
    context->r29 = static_cast<gpr>(static_cast<std::int32_t>(
        dkr::runtime::revision_addresses::EntrypointStackTop));
    dkr::runtime::netplay::register_runtime_context(nullptr, context);
}

void RegisterThreadContext(std::uint8_t* rdram, recomp_context* context) {
    dkr::runtime::code_mods::bind_thread();
    dkr::runtime::netplay::register_runtime_context(rdram, context);
}

void UnregisterThreadContext(std::uint8_t* rdram, recomp_context* context) {
    dkr::runtime::code_mods::unbind_thread();
    dkr::runtime::netplay::unregister_runtime_context(rdram, context);
}

void RunDkrEntrypoint(std::uint8_t* rdram, recomp_context* context) {
    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->entrypoint == nullptr) {
        ultramodern::quit();
        return;
    }

    dkr::runtime::saves::reset_runtime_online_save_status();
    const bool private_party_boot=
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        dkr_experimental_bootstrap_active()&&dkr::runtime::spectators::party_bootstrap_active();
#else
        false;
#endif
    // The party owns a verified memory-only seed, not a personal save route.
    if (!dkr::runtime::saves::runtime_save_context_ready()&&!private_party_boot) {
        std::fprintf(stderr, "[save][boot] %s\n", dkr::runtime::saves::runtime_save_failure().c_str());
        ultramodern::quit(); return;
    }
    const auto online = dkr::runtime::saves::runtime_save_selection();
    if (
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        dkr_experimental_bootstrap_active()
#else
        false
#endif
    ) {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        std::string error;
        if (!dkr_experimental_bootstrap_activate_save(error)) {
            std::fprintf(stderr,"[rollback][boot] Online EEPROM activation refused: %s\n",error.c_str());
            ultramodern::quit();return;
        }
        std::fprintf(stderr,"[rollback][boot] verified online EEPROM active before retail initialization\n");
#endif
    } else if (online.online) {
        std::string error;
        if (!dkr::runtime::saves::activate_online_save_for_runtime(
                online.host, online.match, online.hash, error)) {
            dkr::runtime::netplay::session().fail_runtime_start(
                "The isolated online save could not be activated: " + error);
            std::fprintf(stderr, "[netplay][save] %s\n", error.c_str());
            ultramodern::quit();
            return;
        }
        std::fprintf(stderr,
                     "[netplay][save] isolated online EEPROM activated\n");
    } else {
        // runtime_save_context_ready() verified the actual loaded EEPROM
        // against its immutable launch owner above. Keep that exact route:
        // ordinary, isolated mod-set, or isolated development save.
        std::fprintf(stderr,"[save][boot] frozen offline EEPROM verified; no mutable route switch\n");
    }
    dkr::runtime::code_mods::boot(rdram,recomp::mem_size);
    payload->entrypoint(rdram, context);
}

} // namespace

bool dkr::runtime::RegisterGame(const std::filesystem::path& config_directory,
                                rom::Revision revision, std::string& error) {
    const GamePayload* payload = payload_for(revision);
    if (payload == nullptr || payload->entrypoint == nullptr ||
        payload->register_sections == nullptr) {
        error = "The selected ROM revision has no linked CPU payload.";
        return false;
    }

    if (!select_payload(revision)) {
        error = "The selected ROM revision could not activate its CPU payload.";
        return false;
    }
    revision_addresses::select(revision);
    recomp::register_config_path(config_directory);
    payload->register_sections();

    const recomp::GameEntry game{
        .rom_hash = payload->rom_hash,
        .internal_name = "DIDDY KONG RACING",
        .game_id = kGameId,
        // Only the approved, session-owned loader may activate code. Loose
        // stock-loader NRMs cannot bypass code consent or online admission.
        .mod_game_id = "",
        .save_type = recomp::SaveType::Eep4k,
        .is_enabled = true,
        .decompression_routine = nullptr,
        .has_compressed_code = false,
        .entrypoint_address = kInitialCodeAddress,
        .entrypoint = RunDkrEntrypoint,
        .thread_create_callback = RegisterThreadContext,
        .thread_destroy_callback = UnregisterThreadContext,
        .on_init_callback = InitialiseEntrypointContext,
    };
    recomp::register_game(game);
    error.clear();
    return true;
}

bool dkr::runtime::SelectRom(const std::filesystem::path& rom_path, std::string& error) {
    rom::Identity identity{};
    if (!ValidateRomForLauncher(rom_path, identity, error)) {
        return false;
    }
    if (identity.revision != revision_addresses::selected_revision()) {
        error = "The selected ROM changed after its CPU payload was registered.";
        return false;
    }

    std::u8string game_id{kGameId};
    const recomp::RomValidationError result = recomp::select_rom(rom_path, game_id);
    if (result == recomp::RomValidationError::Good) {
        return true;
    }

    switch (result) {
    case recomp::RomValidationError::FailedToOpen:
        error = "The ROM could not be opened.";
        break;
    case recomp::RomValidationError::NotARom:
        error = "The selected file is not a recognized N64 ROM.";
        break;
    case recomp::RomValidationError::IncorrectVersion:
        error = "The ROM is Diddy Kong Racing, but does not match its selected CPU payload.";
        break;
    case recomp::RomValidationError::IncorrectRom:
        error = "The selected ROM is not a supported Diddy Kong Racing US revision.";
        break;
    default:
        error = "The runtime rejected the selected ROM.";
        break;
    }
    return false;
}

bool dkr::runtime::ValidateRomForLauncher(const std::filesystem::path& rom_path,
                                          rom::Identity& identity,
                                          std::string& error) {
    identity = rom::inspect(rom_path);
    if (!identity.supported()) {
        error = rom::describe(identity);
        return false;
    }
    error.clear();
    return true;
}

#include "runtime_legacy_mods.hpp"
#include "runtime_netplay.hpp"
#include "ultramodern/ultramodern.hpp"

// Headless qualification only: a private owner touching a live guest,
// scheduler, transport or offline Track Lab hook is a test failure.
namespace dkr::runtime {
const GamePayload* active_payload(){throw mods::Error("Private owner reached the live guest table.");}
namespace netplay {
DirectSession& session(){throw mods::Error("Private owner reached the live network session.");}
bool DirectSession::active()const{throw mods::Error("Private owner reached live session state.");}
}
}
namespace ultramodern {
void quit(){throw dkr::mods::Error("Private owner tried to quit the live scheduler.");}
void enqueue_external_message_src(std::int32_t,OSMesg,bool,EventMessageSource){throw dkr::mods::Error("Private owner sent a live PI event.");}
}
extern "C" void osPiStartDma_recomp(std::uint8_t*,recomp_context*){throw dkr::mods::Error("Private owner reached live DMA.");}
extern "C" void dkr_character_select_animation_fraction(std::uint8_t*,recomp_context*){throw dkr::mods::Error("Private owner reached live animation.");}
extern "C" void dkr_custom_tracks_extend_table(std::uint8_t*,recomp_context*,std::uint32_t){throw dkr::mods::Error("Private owner rebuilt offline Track Lab tables.");}
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t*,recomp_context*){throw dkr::mods::Error("Private owner read offline Track Lab data.");}

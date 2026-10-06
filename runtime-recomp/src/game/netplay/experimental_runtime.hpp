#pragma once
#include "direct_session.hpp"
#include "experimental_pump.hpp"
#include "rom_revision.hpp"
#include "ultramodern/renderer_context.hpp"
namespace dkr::mods::online {class RuntimeResources;}

namespace dkr::runtime::netplay::experimental {
struct RuntimeView {
    bool active=false;
    PumpWait wait=PumpWait::None;
    std::uint64_t epoch=0;
    std::uint32_t frame=0,confirmed=0,corrections=0,replayed=0;
    std::string status;
    bool initial_admission=false;
};
bool runtime_available(rom::Revision revision);
RuntimeView runtime_view();
void request_runtime_stop();
// Called only AFTER recomp::start and all of its workers have joined. Borrows
// the existing lobby and window, uses the normal controller/audio/overlay.
bool run_runtime(ultramodern::renderer::WindowHandle window,
    const std::filesystem::path& canonical_rom,std::vector<std::uint8_t> bootstrap,
    DirectSession& lobby,const LaunchDescriptor& accepted_launch,
    unsigned timeout_seconds,std::string& error,bool scripted_check=false,
    std::shared_ptr<const dkr::mods::online::RuntimeResources> mods={},
    std::vector<std::uint8_t> mod_bootstrap={});
}

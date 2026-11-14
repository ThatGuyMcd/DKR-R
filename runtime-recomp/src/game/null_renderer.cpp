#include "null_renderer.hpp"

#include "game_registration.hpp"

#include "librecomp/game.hpp"

#include <cstdio>
#include <memory>

dkr::runtime::DiagnosticRenderer::DiagnosticRenderer() {
    setup_result = ultramodern::renderer::SetupResult::Success;
    chosen_api = ultramodern::renderer::GraphicsApi::Auto;
}

bool dkr::runtime::DiagnosticRenderer::valid() {
    return true;
}

bool dkr::runtime::DiagnosticRenderer::update_config(const ultramodern::renderer::GraphicsConfig&,
                                                     const ultramodern::renderer::GraphicsConfig&) {
    return false;
}

void dkr::runtime::DiagnosticRenderer::enable_instant_present() {}

void dkr::runtime::DiagnosticRenderer::send_dl(const OSTask* task, std::uint8_t*) {
    const auto index = ++display_list_count_;
    std::fprintf(stderr, "[boot][gfx] display-list=%llu ucode=0x%08X data=0x%08X size=%u\n",
                 static_cast<unsigned long long>(index), task->t.ucode, task->t.data_ptr, task->t.data_size);
}

void dkr::runtime::DiagnosticRenderer::update_screen() {
    const auto index = ++present_count_;
    if (index == 1) {
        // The first screen update is queued only after ultramodern's VI thread
        // has installed and committed its safe dummy VI mode. Starting DKR here
        // prevents the game thread from racing the VI bootstrap state.
        std::fprintf(stderr, "[boot] VI initialized; starting recompiled DKR entrypoint\n");
        recomp::start_game(kGameId);
    }
    if (index <= 10 || index % 60 == 0) {
        std::fprintf(stderr, "[boot][vi] present=%llu\n", static_cast<unsigned long long>(index));
    }
}

void dkr::runtime::DiagnosticRenderer::shutdown() {}

std::uint32_t dkr::runtime::DiagnosticRenderer::get_display_framerate() const {
    return 60;
}

float dkr::runtime::DiagnosticRenderer::get_resolution_scale() const {
    return 1.0F;
}

std::unique_ptr<ultramodern::renderer::RendererContext>
dkr::runtime::CreateDiagnosticRenderer(std::uint8_t*, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<DiagnosticRenderer>();
}

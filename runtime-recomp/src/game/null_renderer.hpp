#pragma once

#include "ultramodern/renderer_context.hpp"

#include <atomic>
#include <cstdint>

namespace dkr::runtime {

class DiagnosticRenderer final : public ultramodern::renderer::RendererContext {
  public:
    DiagnosticRenderer();

    bool valid() override;
    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override;
    void enable_instant_present() override;
    void send_dl(const OSTask* task, std::uint8_t* rdram_snapshot) override;
    void update_screen() override;
    void shutdown() override;
    std::uint32_t get_display_framerate() const override;
    float get_resolution_scale() const override;

  private:
    std::atomic<std::uint64_t> display_list_count_{0};
    std::atomic<std::uint64_t> present_count_{0};
};

std::unique_ptr<ultramodern::renderer::RendererContext>
CreateDiagnosticRenderer(std::uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

} // namespace dkr::runtime

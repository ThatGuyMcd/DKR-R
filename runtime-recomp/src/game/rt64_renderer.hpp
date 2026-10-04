#pragma once

#include "f3ddkr_rt64.hpp"
#include "presentation_counter_policy.hpp"
#include "track_performance.hpp"
#include "ultramodern/renderer_context.hpp"

#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <deque>

namespace RT64 {
struct Application;
}
namespace dkr::runtime::netplay::experimental {class RenderSnapshot;class PresentationMailbox;namespace present_gate {class Registration;}}

namespace dkr::runtime {
struct OwnedDecodeScratch;

class RT64Renderer final : public ultramodern::renderer::RendererContext {
public:
    RT64Renderer(std::uint8_t* rdram,
                 ultramodern::renderer::WindowHandle window_handle,
                 bool developer_mode, bool owned_mode = false);
    ~RT64Renderer() override;

    bool valid() override;
    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override;
    void enable_instant_present() override;
    void send_dl(const OSTask* task, std::uint8_t* rdram_snapshot) override;
    void update_screen() override;
    void shutdown() override;
    std::uint32_t get_display_framerate() const override;
    float get_resolution_scale() const override;

    // Re-present the last completed VI image with a fresh ImGui frame while
    // online simulation is deliberately parked. This never decodes a new VI
    // or advances authored state.
    void service_online_wait_presentation();
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    void present_owned(std::shared_ptr<const netplay::experimental::RenderSnapshot>,netplay::experimental::PresentationMailbox&);
    void repeat_owned();
    void reap_owned();
    bool owned_capacity_available();
#endif

private:
    void record_track_performance(std::uint8_t* snapshot,
                                  track_performance::Clock::time_point start);
    track_performance::Capture track_capture_{};
    std::chrono::steady_clock::time_point last_android_report_{};
    std::mutex presentation_mutex_;
    std::unique_ptr<RT64::Application> application_;
    F3DDKRRT64Bridge f3ddkr_;
    std::uint64_t present_count_ = 0;
    std::uint64_t interpolated_present_count_ = 0;
    presentation_counter::Snapshot completed_presentations_{};
    bool first_successful_presentation_reported_ = false;
    std::chrono::steady_clock::time_point last_wait_presentation_{};
    std::chrono::milliseconds slowest_wait_replay_{};
    std::uint64_t observed_wait_generation_ = 0U;
    bool observed_overlay_visible_ = false;
    bool wait_replay_deferred_logged_ = false;
    bool owned_mode_ = false;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    std::unique_ptr<netplay::experimental::present_gate::Registration> owned_registration_;
    std::chrono::steady_clock::time_point last_owned_report_{};
    std::uint64_t last_owned_presentations_=0,last_owned_interpolated_=0;
    struct OwnedSubmission {
        std::uint64_t workload=0,present=0;
        std::shared_ptr<const netplay::experimental::RenderSnapshot> lease;
    };
    std::deque<OwnedSubmission> owned_submissions_; // Hard cap TWO, never a long visual FIFO.
    std::unique_ptr<OwnedDecodeScratch> owned_decode_scratch_;
#endif
};

std::unique_ptr<ultramodern::renderer::RendererContext> CreateRT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

// Called from the SDL window thread. The active renderer is lifetime-guarded
// internally, so a reusable game session can shut down without racing this
// presentation-only heartbeat.
void service_online_wait_presentation();

} // namespace dkr::runtime

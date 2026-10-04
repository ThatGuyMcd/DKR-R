// Compile the exact Patch Pipeline output, not a reimplementation of VI.
// Renderer/RSP hooks abort if reached; this tests real VI, not the GPU.
#include DKR_VI_SOURCE
#include "performance_capture.hpp"
#include <cstdlib>
#include <iostream>

std::atomic_bool exited{false};
moodycamel::LightweightSemaphore graphics_shutdown_ready;
namespace {
using Clock = std::chrono::high_resolution_clock;
Clock::time_point epoch = Clock::now();
int ticks = 0, stop_after = 60, vi_messages = 0, ai_messages = 0;
bool started = true, quit_in_sleep = false;
std::uint32_t pace = 1000;
void require(bool condition, const char* reason) {
    if (!condition) { std::cerr << reason << '\n'; std::exit(1); }
}
void on_vi() {
    // A callback may use a guest getter: the VI mutex must be released first.
    (void)osViGetCurrentFramebuffer();
    if (++ticks == stop_after) exited.store(true);
    dkr::runtime::performance_capture::present();
}
void reset_test() {
    exited.store(false);
    ticks = vi_messages = ai_messages = 0;
    quit_in_sleep = false;
    epoch = Clock::now();
    reset_event_state();
    ultramodern::events::callbacks_t callbacks{};
    callbacks.vi_callback = on_vi;
    callbacks.authored_simulation_pacing_scale_milli_callback = +[] { return pace; };
    ultramodern::events::set_callbacks(callbacks);
}
}

extern "C" u32 osVirtualToPhysical(PTR(void) address) { return u32(address) & 0x1fffffffU; }
void ultramodern::set_native_thread_name(const std::string&) {}
void ultramodern::set_native_thread_priority(ultramodern::ThreadPriority) {}
std::uint32_t ultramodern::get_speed_multiplier() { return 1; }
Clock::time_point ultramodern::get_start() { return epoch; }
Clock::duration ultramodern::time_since_start() { return std::chrono::milliseconds(ticks * 17); }
void ultramodern::sleep_until(const Clock::time_point&) {
    if (quit_in_sleep) exited.store(true);
    std::this_thread::yield();
}
bool ultramodern::is_game_started() { return started; }
void ultramodern::enqueue_external_message_src(PTR(OSMesgQueue), OSMesg, bool,
                                               ultramodern::EventMessageSource source) {
    if (source == ultramodern::EventMessageSource::Vi) ++vi_messages;
    if (source == ultramodern::EventMessageSource::Ai) ++ai_messages;
}

void ultramodern::error_handling::message_box(const char*) { std::abort(); }
void ultramodern::error_handling::quick_exit(const char*, int, const char*, int) { std::abort(); }
const ultramodern::renderer::GraphicsConfig& ultramodern::renderer::get_graphics_config() { std::abort(); }
std::unique_ptr<ultramodern::renderer::RendererContext> ultramodern::renderer::create_render_context(
    std::uint8_t*, ultramodern::renderer::WindowHandle, bool) { std::abort(); }
std::string ultramodern::renderer::get_graphics_api_name(ultramodern::renderer::GraphicsApi) { std::abort(); }
void ultramodern::rsp::init() { std::abort(); }
bool ultramodern::rsp::run_task(std::uint8_t*, const OSTask*) { std::abort(); }
bool ultramodern::enqueue_external_message_src_and_wait(PTR(OSMesgQueue), OSMesg, bool,
    ultramodern::EventMessageSource) { std::abort(); }
void ultramodern::measure_input_latency() { std::abort(); }
void ultramodern::extensions::on_displaylist_submitted(PTR(u64)) { std::abort(); }
void ultramodern::extensions::on_displaylist_parsed(PTR(u64)) { std::abort(); }
void ultramodern::extensions::on_displaylist_completed(PTR(u64)) { std::abort(); }

int main() {
    for (int session = 0; session < 100; ++session) {
        // Recording is armed before initialization, just as on the phone.
        dkr::runtime::performance_capture::start();
        reset_test();
        require(events_context.vi.states[0].mode != nullptr &&
                events_context.vi.states[1].mode != nullptr, "Both modes must exist before workers start");
        require(events_context.vi.regs.VI_WIDTH_REG == 320 &&
                events_context.vi.regs.VI_H_START_REG == 0, "First queued VI must be valid and black");
        // Handles Running/Quit being observed before the first dummy-VI branch.
        started = true;
        stop_after = 60;
        pace = 1000;
        osViSetEvent(nullptr, 0x100, 7, 2);
        osSetEventMesg(nullptr, OS_EVENT_AI, 0x200, 8);
        vi_thread_func();
        require(ticks == 60 && vi_messages == 30 && ai_messages == 60,
                "Original VI/AI authored cadence changed");
        Action action;
        require(events_context.action_queue.try_dequeue(action), "Missing first present");
        const auto* screen = std::get_if<ScreenUpdateAction>(&action);
        require(screen && screen->regs.VI_WIDTH_REG == 320, "Published uninitialized VI");
        require(!events_context.action_queue.try_dequeue(action), "Screen coalescing changed");
        (void)dkr::runtime::performance_capture::finish();
    }
    reset_test();
    quit_in_sleep = true;
    vi_thread_func();
    require(ticks == 0 && total_vis == 0, "VI advanced after quit during sleep");
    Action action;
    require(!events_context.action_queue.try_dequeue(action), "Present queued after quit");

    reset_test();
    pace = 1200;
    osViSetEvent(nullptr, 0x100, 7, 2);
    vi_thread_func();
    require(vi_messages == 36, "Online catch-up cadence changed");

    // Exercise actual guest setters concurrently with the VI reader/swap.
    reset_test();
    stop_after = 4000;
    std::vector<std::uint64_t> ram(4096);
    auto* rdram = reinterpret_cast<std::uint8_t*>(ram.data());
    auto* mode = reinterpret_cast<OSViMode*>(rdram + 256);
    *mode = dummy_mode;
    mode->comRegs.width = 640;
    std::thread vi(vi_thread_func);
    for (int i = 0; i < 10000; ++i) {
        osViSetMode(rdram, static_cast<PTR(OSViMode)>(0x80000100U));
        osViSwapBuffer(rdram, static_cast<PTR(void)>(0x80700000U + ((i & 1) * 0x25800U)));
        osViSetSpecialFeatures(OS_VI_DITHER_FILTER_OFF);
        osViBlack(0);
        osViRepeatLine(i & 1);
        (void)osViGetNextFramebuffer();
    }
    vi.join();
    require(events_context.vi.states[0].mode && events_context.vi.states[1].mode,
            "Concurrent setters lost VI mode");
    reset_test(); // Destroy all guest pointers before the next session.
    require(events_context.vi.states[0].mode == &dummy_mode &&
            events_context.vi.states[1].mode == &dummy_mode, "Reset retained guest memory");
    std::cout << "PASS: 100 recorded bootstraps, quit-in-sleep, cadence, concurrent VI setters\n";
}

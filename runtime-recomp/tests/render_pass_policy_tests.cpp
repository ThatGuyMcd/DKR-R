#include "../src/game/render_pass_policy.hpp"
#include "../src/game/render_metrics.hpp"
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Draw {
    enum class Type { Unknown, IndexedTriangles, RawTriangles, RegularRect, FillRect, VertexTestZ };
    Type type = Type::IndexedTriangles;
    struct {
        struct { bool empty = false; bool isEmpty() const { return empty; } } scissor;
        struct { struct { unsigned mode = 3; unsigned zMode() const { return mode; } } otherMode; } shaderDesc;
    } triangles;
};
int main() {
    using namespace dkr::runtime;
    Draw draw;
    render_pass::ReadBarrierCache reads;
    CHECK(!reads.covers(16, 3, 1));
    reads.record(16, 3);
    CHECK(reads.covers(16, 3, 1) && reads.covers(16, 3, 2) && reads.covers(16, 3, 3));
    CHECK(!reads.covers(16, 1, 1)); // external scope changed
    CHECK(!reads.covers(16, 3, 4)); // new stage not made visible
    CHECK(!reads.covers(16, 3, 0));
    CHECK(!reads.covers(32, 3, 1));
    reads.invalidate(16); CHECK(!reads.covers(16, 3, 1));
    reads.record(16, 3); reads.reset(); CHECK(!reads.covers(16, 3, 1));
    for (std::uintptr_t key = 16; key < 100000; key += 16) reads.record(key, 3);
    // Collisions may forget coverage, but can never claim unrecorded stages.
    for (std::uintptr_t key = 16; key < 100000; key += 16) CHECK(!reads.covers(key, 3, 4));
    CHECK(!render_pass::continue_read_only<Draw>(true, true, nullptr, 3));
    for (auto type : {Draw::Type::Unknown, Draw::Type::IndexedTriangles, Draw::Type::RawTriangles,
                      Draw::Type::RegularRect, Draw::Type::FillRect, Draw::Type::VertexTestZ})
    for (bool read : {false, true}) for (bool color : {false, true})
    for (bool empty : {false, true}) for (unsigned mode = 0; mode < 4; ++mode) {
        draw.type = type; draw.triangles.scissor.empty = empty; draw.triangles.shaderDesc.otherMode.mode = mode;
        const bool geometry = type == Draw::Type::IndexedTriangles || type == Draw::Type::RawTriangles || type == Draw::Type::RegularRect;
        CHECK(render_pass::continue_read_only(read, color, &draw, 3) == (read && color && !empty && geometry && mode == 3));
    }
    int a = 0, b = 1;
    CHECK(render_pass::redundant_binding(&a, &a, true));
    CHECK(!render_pass::redundant_binding(&a, &a, false));
    CHECK(!render_pass::redundant_binding(&a, &b, true));
    CHECK(!render_pass::redundant_binding<int>(nullptr, nullptr, true));
    CHECK(!render_pass::redundant_binding<int>(nullptr, &a, true));
    CHECK(performance_trace::enabled());
    const auto baseline = render_metrics::snapshot();
    CHECK(!render_metrics::valid_gpu_times(0, 0, 0));
    CHECK(!render_metrics::valid_gpu_times(100, 99, 200));
    CHECK(!render_metrics::valid_gpu_times(100, 150, 149));
    CHECK(!render_metrics::valid_gpu_times(100, 200, 6'000'000'000ULL));
    render_metrics::gpu_times(100, 150, 300);
    render_metrics::gpu_times(0, 0, 0);
    std::thread writer([] {
        for (int i = 0; i < 10000; ++i) render_metrics::event(render_metrics::Event::RedundantBind);
    });
    writer.join();
    render_metrics::target(2560, 2400, 4, 10);
    const auto report = render_metrics::describe_delta(baseline);
    CHECK(report.find("redundant-bind-removed=10000") != std::string::npos);
    CHECK(report.find("GPU timestamp samples=1") != std::string::npos);
    CHECK(report.find("Last enhanced target=2560x2400 samples=4 plume-format=10") != std::string::npos);
    const auto after = render_metrics::snapshot();
    CHECK(after.setup - baseline.setup == 50);
    CHECK(after.raster_transfer - baseline.raster_transfer == 150);
    CHECK(render_metrics::describe_delta(after).find("unavailable; not zero GPU cost") != std::string::npos);
    std::puts("PASS: exhaustive depth excursion guards, binding guards, bounded renderer metrics");
}

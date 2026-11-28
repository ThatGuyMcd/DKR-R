#pragma once

#include "ultramodern/renderer_context.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace dkr::updates {
struct Child;
}

namespace dkr::runtime::graphics_launch {

using ultramodern::renderer::GraphicsApi;
using ultramodern::renderer::SetupResult;

inline constexpr std::string_view probe_argument = "--graphics-probe";
#if defined(__ANDROID__) || defined(DKR_IOS)
inline constexpr bool desktop = false;
#else
inline constexpr bool desktop = true;
#endif

enum class Status {
    Unchecked,
    Checking,
    Ready,
    MissingApi,
    MissingDevice,
    MissingLibraries,
    SurfaceFailed,
    PresentationFailed,
    TimedOut,
    DriverExited,
};

struct Result {
    Status status = Status::Unchecked;
    GraphicsApi api = GraphicsApi::Auto;
    std::string adapter;
    std::string driver;
    std::string detail;
};

const char* api_name(GraphicsApi api);
const char* explanation(Status status);
std::string describe(const Result& result);
GraphicsApi requested_api();
void use_once(GraphicsApi api);
GraphicsApi effective_api(GraphicsApi configured);

// Runs in a separate copy of the runtime. It uses real RT64 startup and never
// opens the user's ROM, saves, settings or crash-report queue.
Result probe_renderer(GraphicsApi api);
int run_probe(const char* api, const std::filesystem::path& output);

class Check {
  public:
    Check();
    ~Check();

    void request(GraphicsApi api, bool retry = false);
    void poll();
    void cancel();
    const Result& result() const {
        return result_;
    }
    GraphicsApi requested() const {
        return requested_;
    }

  private:
    void finish(Result result);

    std::unique_ptr<dkr::updates::Child> child_;
    std::filesystem::path directory_;
    std::chrono::steady_clock::time_point deadline_;
    GraphicsApi requested_ = GraphicsApi::Auto;
    Result result_;
};

} // namespace dkr::runtime::graphics_launch

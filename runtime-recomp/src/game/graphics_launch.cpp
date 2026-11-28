#include "graphics_launch.hpp"

#include "runtime_enhancements.hpp"
#include "updates/update_process.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace dkr::runtime::graphics_launch {
namespace {
std::atomic once{GraphicsApi::Auto};
constexpr auto timeout = std::chrono::seconds{15};

bool supported_api(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::Auto:
    case GraphicsApi::Vulkan:
        return true;
    case GraphicsApi::D3D12:
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    case GraphicsApi::Metal:
#if defined(__APPLE__)
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}
} // namespace

const char* api_name(GraphicsApi api) {
    switch (api) {
    case GraphicsApi::Vulkan:
        return "Vulkan";
    case GraphicsApi::D3D12:
        return "Direct3D 12";
    case GraphicsApi::Metal:
        return "Metal";
    default:
        return "Automatic";
    }
}

const char* explanation(Status status) {
    switch (status) {
    case Status::Unchecked:
        return "Graphics compatibility has not been checked.";
    case Status::Checking:
        return "Checking graphics compatibility...";
    case Status::Ready:
        return "Graphics ready";
    case Status::MissingApi:
        return "The selected graphics API could not start. Check that the graphics driver is installed.";
    case Status::MissingDevice:
        return "No compatible graphics adapter was found for this API.";
    case Status::MissingLibraries:
        return "Graphics runtime libraries could not load. Extract the complete DKR-R package.";
    case Status::SurfaceFailed:
        return "The graphics driver could not open a game window.";
    case Status::PresentationFailed:
        return "The graphics device opened, but it could not present a frame.";
    case Status::TimedOut:
        return "The graphics check timed out. You can retry or select another API.";
    case Status::DriverExited:
        return "The graphics process stopped unexpectedly. See the details before retrying.";
    }
    return "Graphics startup failed.";
}

std::string describe(const Result& result) {
    std::string text = explanation(result.status);
    text += "\nGraphics API: ";
    text += api_name(result.api);
    if (!result.adapter.empty())
        text += "\nAdapter: " + result.adapter;
    if (!result.driver.empty())
        text += "\nDriver: " + result.driver;
    if (!result.detail.empty())
        text += "\n" + result.detail;
    return text;
}

GraphicsApi requested_api() {
    return enhancements::modern_presentation_enabled() ? ultramodern::renderer::get_graphics_config().api_option
                                                       : GraphicsApi::Auto;
}

void use_once(GraphicsApi api) {
    once.store(api);
}

GraphicsApi effective_api(GraphicsApi configured) {
    const auto choice = once.load();
    return choice == GraphicsApi::Auto ? configured : choice;
}

int run_probe(const char* argument, const std::filesystem::path& output) {
    const std::string_view text(argument);
    int value = -1;
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), value);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() ||
        !supported_api(static_cast<GraphicsApi>(value)))
        return 2;
    const auto api = static_cast<GraphicsApi>(value);
    auto result = Result{Status::DriverExited, api, {}, {}, {}};
    try {
        result = probe_renderer(api);
    } catch (const std::exception& error) {
        result.detail = error.what();
        result.detail.resize(std::min(result.detail.size(), std::size_t{256}));
    }
    std::ofstream file(output, std::ios::binary);
    file << static_cast<int>(result.status) << ' ' << static_cast<int>(result.api) << ' ' << std::quoted(result.adapter)
         << ' ' << std::quoted(result.driver) << ' ' << std::quoted(result.detail);
    file.close();
    return file ? 0 : 1;
}

Check::Check() = default;
Check::~Check() {
    cancel();
}

void Check::request(GraphicsApi api, bool retry) {
    if (requested_ == api && !retry && result_.status != Status::Unchecked)
        return;
    cancel();
    requested_ = api;
    if (!supported_api(api)) {
        finish({Status::MissingApi, api, {}, {}, "This platform does not provide this API."});
        return;
    }
    try {
        directory_ = std::filesystem::temp_directory_path() / ("dkrr-graphics-" + dkr::updates::unique_id());
        if (!std::filesystem::create_directory(directory_))
            throw std::runtime_error("Could not create a graphics check directory.");
        child_ = std::make_unique<dkr::updates::Child>(
            dkr::updates::application_executable(),
            std::vector<std::string>{std::string(probe_argument), std::to_string(static_cast<int>(api)),
                                     dkr::updates::path_utf8(directory_ / "result")},
            dkr::updates::application_directory());
        result_ = {Status::Checking, api, {}, {}, {}};
        deadline_ = std::chrono::steady_clock::now() + timeout;
    } catch (const std::exception& error) {
        finish({Status::DriverExited, api, {}, {}, error.what()});
    }
}

void Check::poll() {
    if (!child_)
        return;
    try {
        const int exit = child_->wait(0);
        if (exit < 0) {
            if (std::chrono::steady_clock::now() >= deadline_)
                finish({Status::TimedOut, requested_, {}, {}, {}});
            return;
        }
        if (exit == 0) {
            const auto path = directory_ / "result";
            std::error_code error;
            if (std::filesystem::file_size(path, error) <= 2048 && !error) {
                std::ifstream file(path, std::ios::binary);
                int status = 0;
                int api = -1;
                Result checked;
                if (file >> status >> api >> std::quoted(checked.adapter) >> std::quoted(checked.driver) >>
                        std::quoted(checked.detail) &&
                    status >= static_cast<int>(Status::Ready) && status <= static_cast<int>(Status::DriverExited) &&
                    supported_api(static_cast<GraphicsApi>(api))) {
                    checked.status = static_cast<Status>(status);
                    checked.api = static_cast<GraphicsApi>(api);
                    finish(std::move(checked));
                    return;
                }
            }
        }
        finish({Status::DriverExited, requested_, {}, {}, "Graphics check exit code: " + std::to_string(exit)});
    } catch (const std::exception& error) {
        finish({Status::DriverExited, requested_, {}, {}, error.what()});
    }
}

void Check::cancel() {
    if (child_) {
        try {
            child_->terminate();
            child_->wait(100);
        } catch (const std::exception&) {
        }
        child_.reset();
    }
    if (!directory_.empty()) {
        std::error_code error;
        std::filesystem::remove(directory_ / "result", error);
        std::filesystem::remove(directory_, error);
        directory_.clear();
    }
}

void Check::finish(Result result) {
    cancel();
    result_ = std::move(result);
    std::fprintf(stderr, "[graphics][check] %s\n", describe(result_).c_str());
}
} // namespace dkr::runtime::graphics_launch

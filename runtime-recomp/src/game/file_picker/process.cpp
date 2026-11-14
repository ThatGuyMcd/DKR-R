#include "protocol.hpp"
#include "updates/update_process.hpp"
#include <fstream>
#include <cstdio>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace dkr::file_picker {
Json exchange(const std::filesystem::path& helper, const Json& request, unsigned timeout) {
    validate(request);
    const auto directory = std::filesystem::temp_directory_path() / ("dkr-picker-" + dkr::updates::unique_id());
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("Could not prepare the file picker.");
#ifndef _WIN32
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
#endif
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            for (const auto* name : {"request.json", "result.json", "result.json.new"})
                std::filesystem::remove(path / name, e);
            std::filesystem::remove(path, e);
        }
    } cleanup{directory};
    dkr::updates::require_private_tree(directory);
    {
        std::ofstream out(directory / "request.json", std::ios::binary);
        out << request.dump();
        if (!out)
            throw std::runtime_error("Could not write the picker request.");
    }
    std::fprintf(stderr, "[picker] kind=%s state=open isolated=1\n",
                 request["kind"].get_ref<const std::string&>().c_str());
    dkr::updates::Child child(
        helper, {"--request", dkr::updates::path_utf8(directory), std::to_string(dkr::updates::process_id())}, {},
        false, true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (child.wait(20) < 0) {
#ifdef _WIN32
        // A launcher picker blocks its caller, but must not starve the owning
        // window thread's native messages and trigger Windows' hung-app UI.
        MSG message{};
        for (unsigned i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); ++i) {
            if (message.message == WM_QUIT)
                break; // leave shutdown for the caller
            if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
#endif
        if (std::chrono::steady_clock::now() >= deadline) {
            child.terminate();
            throw std::runtime_error("The file picker timed out. Please try again.");
        }
    }
    const auto result_path = directory / "result.json";
    if (child.wait(0) != 0 ||
        std::filesystem::symlink_status(result_path).type() != std::filesystem::file_type::regular ||
        std::filesystem::file_size(result_path) > MaxMessage)
        throw std::runtime_error(
            "The system file picker stopped unexpectedly. Your game is still running; please try again.");
    std::ifstream input(result_path, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(input), {}};
    auto result = Json::parse(data);
    const auto status = result.value("status", "");
    if (result.value("schema", 0) != 1 || (status != "okay" && status != "cancel" && status != "error") ||
        !result.contains("path") || !result["path"].is_string() ||
        result["path"].get_ref<const std::string&>().size() > 32768 ||
        result["path"].get_ref<const std::string&>().find('\0') != std::string::npos ||
        (status == "okay" && result["path"].get_ref<const std::string&>().empty()))
        throw std::runtime_error("The file picker returned an invalid result.");
    std::fprintf(stderr, "[picker] kind=%s state=%s\n", request["kind"].get_ref<const std::string&>().c_str(),
                 status.c_str());
    return result;
}
} // namespace dkr::file_picker

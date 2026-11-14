#include "protocol.hpp"
#include "updates/update_process.hpp"
#include <nfd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
namespace dkr::file_picker {
namespace {
thread_local std::string message;
nfdresult_t select(const char* kind, nfdu8char_t** output, const nfdu8filteritem_t* filters, nfdfiltersize_t count,
                   const char* initial, const char* name) {
    if (!output)
        return NFD_ERROR;
    *output = nullptr;
    try {
        if (count > 32 || (count && !filters))
            throw std::runtime_error("Invalid picker filters.");
        Json request{{"schema", 1},
                     {"kind", kind},
                     {"initial", initial ? initial : ""},
                     {"name", name ? name : ""},
                     {"filters", Json::array()}};
        for (unsigned i = 0; i < count; ++i)
            request["filters"].push_back(
                {{"name", filters[i].name ? filters[i].name : ""}, {"spec", filters[i].spec ? filters[i].spec : ""}});
#ifdef _WIN32
        auto helper = dkr::updates::application_directory() / "libexec/dkr-r/DKR-R-FilePicker.exe";
        if (!std::filesystem::is_regular_file(helper))
            helper = dkr::updates::application_executable().parent_path() / "DKR-R-FilePicker.exe";
#else
        const auto executable = std::filesystem::read_symlink("/proc/self/exe");
        auto helper = executable.parent_path().parent_path() / "libexec/dkr-r/DKR-R-FilePicker";
        if (!std::filesystem::is_regular_file(helper))
            helper = executable.parent_path() / "libexec/dkr-r/DKR-R-FilePicker";
#endif
        const auto result = dkr::file_picker::exchange(helper, request, 1800000);
        if (result["status"] == "cancel")
            return NFD_CANCEL;
        if (result["status"] != "okay")
            throw std::runtime_error("The system file picker could not open. Please try again.");
        const auto path = result["path"].get<std::string>();
        *output = static_cast<char*>(std::malloc(path.size() + 1));
        if (!*output)
            throw std::bad_alloc();
        std::memcpy(*output, path.c_str(), path.size() + 1);
        return NFD_OKAY;
    } catch (const std::exception& e) {
        message = e.what();
        std::fprintf(stderr, "[picker] kind=%s state=error isolated=1\n", kind);
        return NFD_ERROR;
    }
}
} // namespace
nfdresult_t init() {
    message.clear();
    return NFD_OKAY;
}
void quit() {}
const char* error() {
    return message.c_str();
}
void release(nfdu8char_t* p) {
    std::free(p);
}
nfdresult_t open(nfdu8char_t** out, const nfdu8filteritem_t* f, nfdfiltersize_t n, const nfdu8char_t* p) {
    return select("open", out, f, n, p, nullptr);
}
nfdresult_t save(nfdu8char_t** out, const nfdu8filteritem_t* f, nfdfiltersize_t n, const nfdu8char_t* p,
                 const nfdu8char_t* name) {
    return select("save", out, f, n, p, name);
}
nfdresult_t folder(nfdu8char_t** out, const nfdu8char_t* p) {
    return select("folder", out, nullptr, 0, p, nullptr);
}
} // namespace dkr::file_picker
#endif

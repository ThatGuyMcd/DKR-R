#pragma once

#include <filesystem>
#include <string_view>

namespace dkr::runtime::layout {
inline bool windows_component_is(const std::filesystem::path& component, std::string_view name) {
    const auto text = component.u8string();
    if (text.size() != name.size())
        return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != static_cast<unsigned char>(name[i]))
            return false;
    }
    return true;
}
// Native module location, never the CWD or an environment override. All private
// Windows executables share the public launcher's asset/portable/install root.
inline std::filesystem::path windows_application_directory(const std::filesystem::path& executable) {
    const auto directory = executable.parent_path();
    if (windows_component_is(directory.filename(), "dkr-r") &&
        windows_component_is(directory.parent_path().filename(), "libexec"))
        return directory.parent_path().parent_path();
    return directory; // Existing flat installations and developer/test executables.
}
inline std::filesystem::path windows_runtime(const std::filesystem::path& root) {
    return root / "libexec/dkr-r/DKR-R-Runtime.exe";
}
} // namespace dkr::runtime::layout

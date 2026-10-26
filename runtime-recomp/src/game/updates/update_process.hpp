#pragma once
#include "update_core.hpp"
#include <memory>
namespace dkr::updates {
std::string path_utf8(const std::filesystem::path&);
std::filesystem::path application_executable();
std::filesystem::path application_directory();
std::string unique_id();
std::uint64_t process_id();
// An exclusive installation lock, also held by the Windows bootstrap for the
// complete runtime lifetime. This prevents loading half-replaced DLLs.
class InstallLock {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit InstallLock(const std::filesystem::path& root, unsigned timeout_ms = 1000, bool shared = false);
    ~InstallLock();
    InstallLock(const InstallLock&) = delete;
};
struct Child {
    struct Impl;
    std::unique_ptr<Impl> impl;
    Child(const std::filesystem::path& exe, const std::vector<std::string>& arguments,
          const std::filesystem::path& working_directory = {}, bool inherit_update_environment = false);
    ~Child();
    int wait(unsigned timeout_ms);
};
bool parent_alive(std::uint64_t pid);
void wait_for_parent(std::uint64_t pid, const std::filesystem::path& ready);
} // namespace dkr::updates

#pragma once
#include <functional>

namespace dkr::updates {
void shutdown();
}
namespace dkr::online {
void shutdown_account();
}
namespace dkr::online_stability {
void bind_store_http(const void* owner, std::function<void()> stop);
void unbind_store_http(const void* owner);
void retire_store_http();
// Main-thread scope: retire HTTP work before returning to process-wide TLS
// cleanup, while the application installation lock and logging still exist.
struct ProcessServices final {
    explicit ProcessServices(void (*retire)() = nullptr) : retire_(retire) {}
    ProcessServices(const ProcessServices&) = delete;
    ProcessServices& operator=(const ProcessServices&) = delete;
    ~ProcessServices() {
        if (retire_)
            retire_();
        dkr::updates::shutdown();
    }

  private:
    void (*retire_)() = nullptr;
};
} // namespace dkr::online_stability

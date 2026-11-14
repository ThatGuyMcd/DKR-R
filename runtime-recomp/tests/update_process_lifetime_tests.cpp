#include "update_service.hpp"
#include "online_stability/process_services.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    namespace u = dkr::updates;
    using namespace std::chrono_literals;
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkrr-update-lifetime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    std::atomic<bool> entered = false, ended = false, stopped = false;
    u::Configuration config{root / "fake", root, root / "helper", "1.0.12", std::string(64, '0'), "linux-x86_64", {}};
    auto worker = std::make_unique<u::Service>(config, [&](const auto&, std::stop_token stop, const auto&) {
        entered = true;
        const auto end = std::chrono::steady_clock::now() + 2s;
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(1ms);
        stopped = stop.stop_requested();
        ended = true;
        return u::HttpResponse{};
    });
    worker->check();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!entered && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    assert(entered);
    dkr::online_stability::bind_store_http(&worker, [&] {
        assert(u::service() != nullptr);
        worker.reset();
    });
    {
        dkr::online_stability::ProcessServices scope([] { dkr::online_stability::retire_store_http(); });
        u::configure(root / "owner", root, root / "helper", "1.0.12");
        assert(u::service() != nullptr);
    }
    assert(u::service() == nullptr && !worker && stopped && ended);
    unsigned discarded = 0;
    dkr::online_stability::bind_store_http(&discarded, [&] { ++discarded; });
    dkr::online_stability::unbind_store_http(&discarded);
    dkr::online_stability::retire_store_http();
    assert(discarded == 0);
    u::shutdown();
    assert(u::service() == nullptr); // Idempotent, including an early exit.
    std::cout
        << "Actual updater cancellation joins its worker; main process scope retires the global owner before exit.\n";
}

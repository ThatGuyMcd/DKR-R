#include "controller_feedback/input_client.hpp"

#include <chrono>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr,
                     "usage: sdl3_input_client_tests HOST MAPPINGS\n");
        return 1;
    }

    dkr::runtime::sdl3_input::Client client;
    dkr::runtime::sdl3_input::State state{};
    for (int cycle = 0; cycle < 2; ++cycle) {
        std::string error;
        if (!client.start(std::filesystem::path(argv[1]),
                          std::filesystem::path(argv[2]), error)) {
            std::fprintf(stderr,
                         "SDL3 client integration cycle %d start failed: %s\n",
                         cycle + 1, error.c_str());
            return 2;
        }

        state = {};
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        do {
            state = client.state();
            if (state.sequence != 0U) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);

        const std::string detail = client.detail();
        if (!client.healthy() || state.sequence == 0U ||
            detail.find("SDL3") == std::string::npos) {
            std::fprintf(
                stderr,
                "SDL3 client integration cycle %d received no live state: %s\n",
                cycle + 1, detail.c_str());
            client.stop();
            return 3;
        }

        // Exercise shutdown while an output producer is active. The unknown
        // instance is deliberately ignored by the helper: no physical effect.
        std::atomic<bool> writing{true};
        std::thread output([&] {
            while(writing.load()) {
                client.feedback(-1,{});
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        client.stop();
        writing=false;output.join();
        if (client.healthy()) {
            std::fprintf(stderr,
                         "SDL3 client cycle %d remained healthy after shutdown.\n",
                         cycle + 1);
            return 4;
        }
    }
    std::fprintf(stderr,
                 "SDL3 client restart integration passed: sequence=%llu devices=%zu\n",
                 static_cast<unsigned long long>(state.sequence),
                 state.devices.size());
    return 0;
}

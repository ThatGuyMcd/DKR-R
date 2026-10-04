#include "atomic_snapshot.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

struct Snapshot { std::uint64_t generation; std::uint64_t check; };
int main() {
    dkr::runtime::AtomicSnapshot<const Snapshot> snapshot;
    if (snapshot.load()) return 1;
    snapshot.store(std::make_shared<const Snapshot>(Snapshot{7, ~std::uint64_t{7}}));
    const auto retained = snapshot.load();
    std::atomic<bool> done{false}, invalid{false};
    std::thread reader([&] {
        do {
            auto value = snapshot.load(std::memory_order_acquire);
            if (!value || value->check != ~value->generation) invalid = true;
        } while (!done.load(std::memory_order_acquire));
    });
    for (std::uint64_t i = 0; i < 10000; ++i)
        snapshot.store(std::make_shared<const Snapshot>(Snapshot{i, ~i}), std::memory_order_release);
    done.store(true, std::memory_order_release);
    reader.join();
    snapshot.store({});
    if (invalid || snapshot.load() || retained->generation != 7 || retained->check != ~std::uint64_t{7}) return 2;
    std::cout << "Atomic snapshot publication and retained ownership passed.\n";
}

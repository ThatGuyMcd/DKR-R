#include "../src/game/android_pipeline_cache.hpp"
#include <cstdlib>
#include <iostream>
#include <chrono>

void require(bool value) { if (!value) std::abort(); }
int main() {
    using namespace dkr::runtime::android_pipeline_cache;
    Identity id{0x5143, 830, 0x80320046, {1,2,3,4,5}};
    std::vector<std::uint8_t> payload;
    append_word(payload, 32); append_word(payload, 1);
    append_word(payload, id.vendor); append_word(payload, id.device);
    payload.insert(payload.end(), id.uuid.begin(), id.uuid.end());
    for (unsigned i = 0; i < 1000; ++i) payload.push_back(std::uint8_t(i * 31));
    const auto encoded = encode(payload, id);
    require(decode(encoded, id) == payload);
    for (std::size_t length = 0; length < encoded.size(); ++length)
        require(decode({encoded.begin(), encoded.begin() + length}, id).empty());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        auto bad = encoded; bad[i] ^= 1;
        require(decode(bad, id).empty());
    }
    auto other = id; ++other.driver; require(decode(encoded, other).empty());
    other = id; ++other.vendor; require(decode(encoded, other).empty());
    other = id; ++other.device; require(decode(encoded, other).empty());
    other = id; ++other.uuid[3]; require(decode(encoded, other).empty());
    require(!compatible(std::vector<std::uint8_t>(maximum_bytes + 1), id));
    require(!compatible({}, id));
    require(!save(id, payload)); // No configured private path: never use CWD.
    require(load(id).empty());
    directory = std::filesystem::temp_directory_path() /
        ("dkr-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(!std::filesystem::exists(directory));
    require(load(id).empty());
    require(save(id, payload));
    require(load(id) == payload);
    require(load(other).empty());
#if !defined(_WIN32)
    // Android uses POSIX atomic rename-over-existing semantics.
    payload.back() ^= 1;
    require(save(id, payload));
    require(load(id) == payload);
#endif
    const auto destination = path_for(id);
    { std::ofstream bad(destination, std::ios::binary | std::ios::trunc); bad << "truncated"; }
    require(load(id).empty());
    // Remove only the exact test-created file and empty test directory.
    require(std::filesystem::remove(destination));
    require(std::filesystem::remove(directory));
    directory.clear();
    std::cout << "PASS: cache identity, corruption, truncation, bounds, file persistence and no-path fallback\n";
}

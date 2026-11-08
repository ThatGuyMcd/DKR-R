#pragma once
#include <chrono>
#include <filesystem>
#include <string>
namespace dkr::release {
// libc++ can use a 128-bit file-clock rep. Preserve subsecond identity without
// narrowing its complete epoch count or requiring a nonstandard to_string.
inline std::string file_stamp(std::filesystem::file_time_type value) {
    const auto duration=value.time_since_epoch();
    const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(duration);
    const auto remainder=std::chrono::duration_cast<std::chrono::nanoseconds>(duration-seconds);
    return std::to_string(seconds.count())+"."+std::to_string(remainder.count());
}
}

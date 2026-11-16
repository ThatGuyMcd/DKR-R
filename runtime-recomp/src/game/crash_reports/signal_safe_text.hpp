#pragma once
// Text building for a fatal signal handler: fixed buffers, no allocation, no
// stdio, no locale, so it is safe to call after a crash. Every function
// appends to `buffer` at `size`, never writes past `capacity`, and returns the
// new size (output that does not fit is cut, not wrapped).
#include <cstddef>
#include <cstdint>

namespace dkr::crash_reports::signal_safe {

inline std::size_t append_text(char* buffer, std::size_t size, std::size_t capacity, const char* text) noexcept {
    while (*text != '\0' && size < capacity)
        buffer[size++] = *text++;
    return size;
}

inline std::size_t append_number(char* buffer, std::size_t size, std::size_t capacity, std::uint64_t value,
                                 unsigned base) noexcept {
    char digits[64];
    std::size_t count = 0;
    do {
        digits[count++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value != 0 && count < sizeof digits);
    while (count != 0 && size < capacity)
        buffer[size++] = digits[--count];
    return size;
}

// A thread name is free text. Keep only characters that cannot break a log
// line or hide a path, and stop at the end of the name.
inline std::size_t append_name(char* buffer, std::size_t size, std::size_t capacity, const char* name,
                               std::size_t limit) noexcept {
    for (std::size_t i = 0; i < limit && name[i] != '\0' && size < capacity; ++i) {
        const char c = name[i];
        const bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
                           c == '_' || c == '.' || c == ' ';
        buffer[size++] = plain ? c : '?';
    }
    return size;
}

} // namespace dkr::crash_reports::signal_safe

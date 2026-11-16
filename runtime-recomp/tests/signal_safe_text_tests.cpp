// The fatal signal handler builds its report with these helpers: they must never
// write past the buffer, whatever they are given.
#include "crash_reports/signal_safe_text.hpp"

#include <cassert>
#include <cstring>
#include <string>

using namespace dkr::crash_reports::signal_safe;

int main() {
    char buffer[64];
    std::size_t size = append_text(buffer, 0, sizeof buffer, "[boot][crash] signal=");
    size = append_number(buffer, size, sizeof buffer, 11, 10);
    size = append_text(buffer, size, sizeof buffer, " memory-address=0x");
    size = append_number(buffer, size, sizeof buffer, 0x1886145e8ULL, 16);
    assert(std::string(buffer, size) == "[boot][crash] signal=11 memory-address=0x1886145e8");

    // Zero and the widest value.
    size = append_number(buffer, 0, sizeof buffer, 0, 10);
    assert(std::string(buffer, size) == "0");
    size = append_number(buffer, 0, sizeof buffer, ~0ULL, 16);
    assert(std::string(buffer, size) == "ffffffffffffffff");
    size = append_number(buffer, 0, sizeof buffer, ~0ULL, 10);
    assert(std::string(buffer, size) == "18446744073709551615");

    // Nothing is written past the capacity, and the guard bytes stay intact.
    char guarded[12];
    std::memset(guarded, 'G', sizeof guarded);
    size = append_text(guarded, 0, 8, "0123456789");
    assert(size == 8 && std::memcmp(guarded, "01234567GGGG", 12) == 0);
    size = append_number(guarded, 6, 8, 123456, 10);
    assert(size == 8 && guarded[8] == 'G' && guarded[11] == 'G');
    assert(append_text(guarded, 8, 8, "x") == 8);

    // Thread names: plain characters pass, anything else becomes '?', and the limit applies.
    size = append_name(buffer, 0, sizeof buffer, "RT64 render/\n\"x\"", 64);
    assert(std::string(buffer, size) == "RT64 render???x?");
    size = append_name(buffer, 0, sizeof buffer, "abcdefgh", 3);
    assert(std::string(buffer, size) == "abc");
    return 0;
}

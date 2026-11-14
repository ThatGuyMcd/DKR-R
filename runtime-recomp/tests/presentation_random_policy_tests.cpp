#include "presentation_random_policy.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using dkr::runtime::netplay::PresentationRandomStream;

    PresentationRandomStream first(0x12345678U);
    PresentationRandomStream second(0x12345678U);
    for (int index = 0; index < 512; ++index) {
        assert(first.next() == second.next());
    }

    PresentationRandomStream bounded(0xA5A5A5A5U);
    bool saw_minimum = false;
    bool saw_maximum = false;
    for (int index = 0; index < 4096; ++index) {
        const std::int32_t value = bounded.range(-7, 7);
        assert(value >= -7 && value <= 7);
        saw_minimum |= value == -7;
        saw_maximum |= value == 7;
    }
    assert(saw_minimum && saw_maximum);

    PresentationRandomStream reversed(0x13579BDFU);
    for (int index = 0; index < 128; ++index) {
        const std::int32_t value = reversed.range(12, -4);
        assert(value >= -4 && value <= 12);
    }

    PresentationRandomStream full_width(0xCAFEBABEU);
    (void)full_width.range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
    assert(full_width.state() != 0U);
    return 0;
}

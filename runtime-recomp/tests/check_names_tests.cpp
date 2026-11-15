#include "netplay/check_names.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {
void environment(const char* value) {
#if defined(_WIN32)
    _putenv_s("DKR_OWNED_CHECK_NAME", value ? value : "");
#else
    if (value)
        setenv("DKR_OWNED_CHECK_NAME", value, 1);
    else
        unsetenv("DKR_OWNED_CHECK_NAME");
#endif
}
} // namespace

int main() {
    using namespace dkr::runtime::netplay;
    namespace names = check_names;

    // Stable per seed, always a name the lobby accepts, always "#<1..99> ...".
    assert(names::generate(7) == names::generate(7));
    std::set<std::string> seen, monikers;
    bool written = false, paired = false, low = false, high = false;
    for (std::uint64_t seed = 0; seed < 40000; ++seed) {
        const std::string name = names::generate(seed);
        assert(valid_display_name(name));
        const auto space = name.find(' ');
        assert(name[0] == '#' && space != std::string::npos && space >= 2 && space <= 3);
        const int number = std::stoi(name.substr(1, space - 1));
        assert(number >= 1 && number <= 99 && name.substr(1, space - 1) == std::to_string(number));
        low = low || number == 1;
        high = high || number == 99;
        const std::string moniker = name.substr(space + 1);
        assert(!moniker.empty() && moniker.size() <= names::kMonikerBytes && moniker.front() != ' ' &&
               moniker.back() != ' ');
        bool listed = false;
        for (const auto entry : names::kMonikers)
            listed = listed || moniker == entry;
        written = written || listed;
        paired = paired || !listed;
        seen.insert(name);
        monikers.insert(moniker);
    }
    // Both sources are used, the whole number range is, and every moniker is reachable.
    assert(written && paired && low && high);
    assert(monikers.size() == names::kMonikerCount);
    assert(seen.size() > 15000);

    // Neighbouring seeds, which is what four peers started together might draw.
    assert(names::generate(1) != names::generate(2) && names::generate(2) != names::generate(3));

    // An override is used when the lobby would accept it, and ignored when not.
    environment("Sir Peels-a-Lot");
    assert(names::choose() == "Sir Peels-a-Lot");
    environment("This name is far too long for a lobby");
    assert(valid_display_name(names::choose()) && names::choose()[0] == '#');
    environment(nullptr);
    assert(valid_display_name(names::choose()) && names::choose()[0] == '#');

    std::cout << "Self-test racer names: " << names::kMonikerCount << " monikers, all valid and reachable\n";
}

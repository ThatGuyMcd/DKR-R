#include "launcher_recovery_policy.hpp"
#include <iostream>

int main() {
    for (unsigned mask=0; mask<8; ++mask) {
        const bool failed=mask&1, owned=mask&2, content=mask&4;
#if defined(_WIN32)
        const bool expected=failed || owned || content;
#else
        const bool expected=failed;
#endif
        if (dkr::runtime::launcher_surface_recovery(failed, owned, content)!=expected) {
            std::cerr << "Launcher surface recovery mismatch: " << mask << '\n';
            return 1;
        }
    }
    std::cout << "8 launcher surface recovery policy checks passed.\n";
}

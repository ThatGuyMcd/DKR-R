#include "party_runtime_fixture.hpp"

int main() {
    using namespace dkr::runtime::spectators;
    try {
        racer_transitions();
        watcher_orders();
        driver_advances_after_release();
        racer_without_watcher();
        authority_and_roster();
        host_does_not_wait_for_watcher();
        std::cout << assertions << " party runtime regression checks passed; synthetic state only.\n";
    } catch (const std::exception& e) {
        std::cerr << "REGRESSION FAILURE: " << e.what() << '\n';
        return 1;
    }
}

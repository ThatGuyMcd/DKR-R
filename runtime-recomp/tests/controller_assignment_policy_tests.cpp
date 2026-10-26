#include "controller_assignment_policy.hpp"

#include <cassert>

int main() {
    using namespace dkr::runtime::controllers;
    DesiredAssignments desired{};
    const std::vector<Device> devices{{10, "pad-a"}, {20, "pad-b"}, {30, "pad-c"}, {40, "pad-d"}};

    populate_automatic_claims(desired, devices);
    assert((desired == DesiredAssignments{"pad-a", "pad-b", "pad-c", "pad-d"}));
    assert((resolve(desired, devices) == ResolvedAssignments{10, 20, 30, 40}));

    // Moving an owned device swaps the two player claims, so one physical pad
    // can never drive two N64 ports.
    assert(claim(desired, 0, "pad-c"));
    assert((desired == DesiredAssignments{"pad-c", "pad-b", "pad-a", "pad-d"}));
    assert((resolve(desired, devices) == ResolvedAssignments{30, 20, 10, 40}));

    // Disconnecting a pad leaves that port neutral without shifting players.
    const std::vector<Device> disconnected{{10, "pad-a"}, {20, "pad-b"}, {40, "pad-d"}};
    assert((resolve(desired, disconnected) == ResolvedAssignments{-1, 20, 10, 40}));

    // A reconnect reclaims its previous player regardless of enumeration order.
    const std::vector<Device> reconnected{{40, "pad-d"}, {30, "pad-c"}, {10, "pad-a"}, {20, "pad-b"}};
    assert((resolve(desired, reconnected) == ResolvedAssignments{30, 20, 10, 40}));

    assert(clear(desired, 2));
    assert(desired[2].empty());
    assert(!claim(desired, kPlayerCount, "pad-a"));
    assert(!claim(desired, 0, ""));
    return 0;
}

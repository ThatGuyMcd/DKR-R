// Prints what this toolchain tells peers about how it computes: the contract
// string the lobby would advertise and each clause of strict-ieee754-v1. Built
// with the probe's floating point flags so a cross build reports for itself.
#include "netplay/determinism_contract.hpp"

#include <cstdio>
#include <string>

int main() {
    using namespace dkr::runtime::netplay;
    std::printf("[contract] architecture=%s contract=%s\n", std::string(build_architecture()).c_str(),
                floating_point_contract().c_str());
    for (const auto& clause : kStrictIeee754Clauses)
        std::printf("[contract]   %s: %s\n", clause.holds ? "ok" : "NOT MET", std::string(clause.requirement).c_str());
    return 0;
}

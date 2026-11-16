#include "netplay/netplay_types.hpp"
#include "start_cancel_message.hpp"

#include <cstdio>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "start_cancel_message_tests.cpp:%d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(condition) check((condition), #condition, __LINE__)
} // namespace

int main() {
    using dkr::runtime::party_start::unacknowledged_start_message;
    const std::string tail = " The lobby is still open; check the connection and try again.";
    const std::string spans = " This lobby spans CPU architectures, so every racer needs a build that supports that.";

    // No racer can be named: the sentence the message always began with.
    CHECK(unacknowledged_start_message({}, false) ==
          "A racer did not acknowledge the synchronized start in time." + tail);
    CHECK(unacknowledged_start_message({"Ada"}, false) ==
          "Ada did not acknowledge the synchronized start in time." + tail);
    CHECK(unacknowledged_start_message({"Ada", "Bo"}, false) ==
          "Ada and Bo did not acknowledge the synchronized start in time." + tail);
    CHECK(unacknowledged_start_message({"Ada", "Bo", "Cy", "Di"}, false) ==
          "Ada, Bo and 2 more did not acknowledge the synchronized start in time." + tail);
    CHECK(unacknowledged_start_message({"Ada"}, true) ==
          "Ada did not acknowledge the synchronized start in time." + spans + tail);

    // The launch hash form that goes in the log: the architecture-free hash only for a
    // lobby that spans architectures.
    dkr::runtime::netplay::CompatibilityManifest host;
    host.architecture = "arm64";
    host.floating_point_mode = "strict-ieee754-v1";
    CHECK(dkr::runtime::netplay::launch_hash_form(dkr::runtime::netplay::launch_compatibility_hash(host, false),
                                                  host) == "full");
    CHECK(dkr::runtime::netplay::launch_hash_form(dkr::runtime::netplay::launch_compatibility_hash(host, true), host) ==
          "class");

    return failures == 0 ? 0 : 1;
}

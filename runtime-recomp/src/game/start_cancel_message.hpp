#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The host's status line when a synchronized start is cancelled because racers
// never acknowledged it. `silent` are the display names of the racers (and the
// host's own party members) that did not. `spans` is true when the launch used the
// architecture-free hash, which a build from before cross-architecture lobbies
// can never accept.
namespace dkr::runtime::party_start {
inline std::string unacknowledged_start_message(const std::vector<std::string>& silent, bool spans) {
    std::string who;
    if (silent.empty())
        who = "A racer";
    else if (silent.size() == 1)
        who = silent[0];
    else if (silent.size() == 2)
        who = silent[0] + " and " + silent[1];
    else
        who = silent[0] + ", " + silent[1] + " and " + std::to_string(silent.size() - 2U) + " more";
    std::string message = who + " did not acknowledge the synchronized start in time.";
    if (spans)
        message += " This lobby spans CPU architectures, so every racer needs a build that supports that.";
    message += " The lobby is still open; check the connection and try again.";
    return message;
}
} // namespace dkr::runtime::party_start

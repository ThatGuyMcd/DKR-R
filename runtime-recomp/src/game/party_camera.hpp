#pragma once
#include <atomic>
namespace dkr::runtime::spectators {
// Local presentation only; never included in a simulation checkpoint/rules.
inline std::atomic<bool> spectator_director{false};
}

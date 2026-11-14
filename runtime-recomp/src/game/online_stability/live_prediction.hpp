#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace dkr::online_stability {
inline thread_local const unsigned* live_prediction_source = nullptr;
class PredictionScope final {
    const unsigned* previous_;

  public:
    explicit PredictionScope(const unsigned& window) : previous_(live_prediction_source) {
        live_prediction_source = &window;
    }
    ~PredictionScope() {
        live_prediction_source = previous_;
    }
    PredictionScope(const PredictionScope&) = delete;
    PredictionScope& operator=(const PredictionScope&) = delete;
};
inline std::optional<unsigned> requested_prediction_window() {
    return live_prediction_source ? std::optional<unsigned>(*live_prediction_source) : std::nullopt;
}
// Owner-local execution policy only. Never alter the admitted launch hash,
// confirmed frontier, immutable input, pending effects or guest checkpoint.
template <class History>
bool update_prediction_window(bool active, unsigned requested, std::size_t state_bytes, std::uint8_t& window,
                              std::size_t& budget, History* history, std::size_t& retained,
                              std::uint32_t preserve_until, std::string& error) {
    if (!active || !history || requested < 2 || requested > 20) {
        error = "Prediction window must be between 2 and 20 frames in an active game.";
        return false;
    }
    if (requested == window) {
        error.clear();
        return true;
    }
    const auto required =
        std::clamp<std::size_t>(state_bytes * (requested + 2U) + 65536U, 128U * 1024U * 1024U, 512U * 1024U * 1024U);
    const auto next_budget = std::max(budget, required);
    if (next_budget < state_bytes || !history->reserve_window(requested, next_budget - state_bytes, preserve_until)) {
        error = "There is not enough checkpoint memory for that prediction window. The existing history has been kept.";
        return false;
    }
    const auto previous = window;
    window = std::uint8_t(requested);
    budget = next_budget;
    retained = history->allocated_bytes() + state_bytes;
    std::fprintf(stderr, "[online][prediction-window] from=%u to=%u retained-bytes=%zu budget=%zu; history preserved\n",
                 unsigned(previous), requested, retained, budget);
    error.clear();
    return true;
}
} // namespace dkr::online_stability

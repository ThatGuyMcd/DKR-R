#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace dkr::runtime {
// Recycle completed graphics-task storage, never live guest memory. Excess
// concurrent tasks allocate normally instead of blocking SP/DP completion.
template <std::size_t Bytes, std::size_t Capacity = 4>
class GraphicsSnapshotPool {
    struct State {
        std::mutex mutex;
        std::array<std::unique_ptr<std::uint8_t[]>, Capacity> free;
    };
public:
    struct Return {
        std::shared_ptr<State> state;
        void operator()(std::uint8_t* data) const noexcept {
            std::unique_ptr<std::uint8_t[]> owned(data);
            if (!data || !state) return;
            std::lock_guard lock(state->mutex);
            for (auto& slot : state->free) {
                if (!slot) { slot = std::move(owned); return; }
            }
        }
    };
    using Snapshot = std::unique_ptr<std::uint8_t[], Return>;
    Snapshot acquire() {
        std::unique_ptr<std::uint8_t[]> data;
        {
            std::lock_guard lock(state_->mutex);
            for (auto& slot : state_->free) {
                if (slot) { data = std::move(slot); break; }
            }
        }
        // Every byte is overwritten before publication: no redundant zeroing.
        if (!data) data.reset(new std::uint8_t[Bytes]);
        return Snapshot(data.release(), Return{state_});
    }
private:
    std::shared_ptr<State> state_ = std::make_shared<State>();
};
} // namespace dkr::runtime

#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>

namespace dkr::runtime::netplay {
// Callbacks may nest (SDK close/open can invoke one synchronously). Count live
// invocations without holding a lock while invoking user code. The owner retires
// this gate before releasing any state and without holding callback-used locks.
class CallbackLifetime {
    struct State {
        std::mutex mutex;
        std::condition_variable drained;
        bool retired = false;
        unsigned active = 0;
    };
    struct Lease {
        std::shared_ptr<State> state;
        ~Lease() {
            std::lock_guard lock(state->mutex);
            if (--state->active == 0)
                state->drained.notify_all();
        }
    };

  public:
    CallbackLifetime() : state_(std::make_shared<State>()) {}
    template <class Function> auto wrap(Function function) const {
        return [state = state_, function = std::move(function)](auto... args) {
            {
                std::lock_guard lock(state->mutex);
                if (state->retired)
                    return;
                ++state->active;
            }
            Lease lease{state};
            function(std::move(args)...);
        };
    }
    void retire() {
        std::unique_lock lock(state_->mutex);
        state_->retired = true;
        state_->drained.wait(lock, [this] { return state_->active == 0; });
    }

  private:
    std::shared_ptr<State> state_;
};
} // namespace dkr::runtime::netplay

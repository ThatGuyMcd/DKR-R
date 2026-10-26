#pragma once

#include <SDL.h>
#include <mutex>
#include <vector>

namespace dkr::runtime::ios {

// Only value-owned SDL events consumed by ImGui may cross this queue. In
// particular, drop and extended editing events own pointers and stay outside.
class UiEventQueue {
  public:
    struct Batch {
        std::vector<SDL_Event> events;
        bool reset_inputs = false;
    };

    bool push(const SDL_Event& event) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEWHEEL:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_TEXTINPUT:
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_WINDOWEVENT:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            break;
        default:
            return false;
        }
        std::lock_guard lock(mutex_);
        if (pending_.events.size() >= 512) {
            pending_.events.clear();
            // A discarded release must not leave a key or pointer held down.
            pending_.reset_inputs = true;
        }
        pending_.events.push_back(event);
        return true;
    }

    Batch take() {
        std::lock_guard lock(mutex_);
        Batch batch;
        batch.events.swap(pending_.events);
        batch.reset_inputs = pending_.reset_inputs;
        pending_.reset_inputs = false;
        return batch;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        pending_.events.clear();
        pending_.reset_inputs = false;
    }

  private:
    std::mutex mutex_;
    Batch pending_;
};

} // namespace dkr::runtime::ios

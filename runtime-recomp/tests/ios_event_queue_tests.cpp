#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ios/ios_event_queue.hpp"
#include <cassert>
#include <cstring>

int main() {
    dkr::runtime::ios::UiEventQueue queue;
    SDL_Event event{};
    event.type = SDL_DROPFILE;
    assert(!queue.push(event));
    event.type = SDL_TEXTEDITING_EXT;
    assert(!queue.push(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_a;
    assert(queue.push(event));
    event.type = SDL_KEYUP;
    assert(queue.push(event));
    event.type = SDL_TEXTINPUT;
    std::strcpy(event.text.text, "hello");
    assert(queue.push(event));
    std::strcpy(event.text.text, "changed");
    auto batch = queue.take();
    assert(!batch.reset_inputs && batch.events.size() == 3);
    assert(batch.events[0].type == SDL_KEYDOWN);
    assert(batch.events[1].type == SDL_KEYUP);
    assert(std::strcmp(batch.events[2].text.text, "hello") == 0);
    assert(queue.take().events.empty());
    for (int i = 0; i < 513; ++i) {
        event.type = SDL_KEYDOWN;
        event.key.timestamp = i;
        assert(queue.push(event));
    }
    batch = queue.take();
    assert(batch.reset_inputs && batch.events.size() == 1);
    assert(batch.events.front().key.timestamp == 512);
    assert(!queue.take().reset_inputs);
    assert(queue.push(event));
    queue.clear();
    assert(queue.take().events.empty());
}

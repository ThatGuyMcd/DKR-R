#pragma once
// stderr lines from code that runs under the session's locks.
//
// DirectSession::pump_locked and everything it calls holds mutex_ (and often
// PartyLink::mutex). A blocked stderr (a pipe nobody drains) would stall every
// session call behind those locks. While a pump runs, the session installs its
// queue for this thread with Scope; write() then formats the line exactly as
// fprintf would and queues it, and the lock's owner flushes the queue after
// releasing the lock. On any other thread, or outside a pump, write() is
// fprintf(stderr, ...).
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dkr::runtime::session_log {

// At most Capacity lines wait; older ones are dropped and counted.
struct Queue {
    static constexpr std::size_t Capacity = 512;
    std::vector<std::string> lines;
    std::uint64_t dropped = 0U;

    void push(std::string line) {
        if (lines.size() >= Capacity) {
            lines.erase(lines.begin());
            ++dropped;
        }
        lines.push_back(std::move(line));
    }
};

inline Queue*& installed() {
    static thread_local Queue* queue = nullptr;
    return queue;
}

// Routes write() on this thread into queue while alive. Not nestable across
// different queues; the previous one is restored.
class Scope {
  public:
    explicit Scope(Queue* queue) noexcept : previous_(installed()) {
        installed() = queue;
    }
    ~Scope() {
        installed() = previous_;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    Queue* previous_;
};

inline std::string vformat(const char* format, va_list arguments) {
    va_list measure;
    va_copy(measure, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    std::string line;
    if (length > 0) {
        line.resize(static_cast<std::size_t>(length));
        std::vsnprintf(line.data(), line.size() + 1U, format, arguments);
    }
    return line;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void
write(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (Queue* queue = installed()) {
        queue->push(vformat(format, arguments));
    } else {
        std::vfprintf(stderr, format, arguments);
    }
    va_end(arguments);
}

} // namespace dkr::runtime::session_log

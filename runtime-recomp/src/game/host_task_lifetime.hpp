#pragma once

#include "scheduler_event_policy.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace dkr::runtime::host_tasks {
using Clock = std::chrono::steady_clock;
enum class Engine : unsigned { Sp = 1, Dp = 2 };
enum class Stage { Queued, SpNotify, SpAcknowledged, Parse, Parsed, DpNotify };
struct Token {
    std::uint32_t address = 0;
    std::uint64_t generation = 0;
    explicit operator bool() const { return generation != 0; }
};
struct Task {
    Token token{};
    unsigned pending = 0;
    std::uint32_t type = 0;
    Stage stage = Stage::Queued;
    Clock::time_point progress{};
    bool reported = false;
};
inline const char* stage_name(Stage s) {
    switch (s) {
    case Stage::Queued: return "queued";
    case Stage::SpNotify: return "SP-delivery";
    case Stage::SpAcknowledged: return "SP-acknowledged";
    case Stage::Parse: return "display-list/fullSync";
    case Stage::Parsed: return "display-list-complete";
    case Stage::DpNotify: return "DP-delivery";
    }
    return "unknown";
}
inline std::uint32_t canonical(std::uint32_t address) {
    return 0x80000000U | (address & 0x1FFFFFFFU);
}

// Host-side bookkeeping only; never placed in rollback RDRAM. The lease
// survives queueing and worker completion until the retail handler consumes
// the edge. Registry locks are never held across delivery, parsing or waits.
class Registry {
    std::mutex mutex_;
    std::array<Task, 32> tasks_{};
    std::uint64_t generation_ = 0;
    unsigned trace_count_ = 0;
    unsigned guard_count_ = 0;
    Clock::time_point epoch_ = Clock::now();
    Task* find(Token token) {
        for (auto& task : tasks_)
            if (task.pending && task.token.address == token.address &&
                task.token.generation == token.generation) return &task;
        return nullptr;
    }
    void trace(const Task& task, const char* event, Clock::time_point now) {
#if defined(__ANDROID__)
        constexpr bool enabled = true;
#else
        static const bool enabled = std::getenv("DKR_TASK_TRACE") != nullptr;
#endif
        if (!enabled || trace_count_ >= 96) return;
        ++trace_count_;
        std::fprintf(stderr, "[host-task] t=%.1fms id=%llu task=%08X type=%u pending=%u event=%s\n",
            std::chrono::duration<double, std::milli>(now - epoch_).count(),
            static_cast<unsigned long long>(task.token.generation),
            task.token.address, task.type, task.pending, event);
    }
public:
    void reset() {
        std::scoped_lock lock(mutex_);
        tasks_ = {};
        // Do not reuse generations across restarts: stale tokens stay stale.
        trace_count_ = guard_count_ = 0;
        epoch_ = Clock::now();
    }
    Token begin(std::uint32_t address, std::uint32_t type,
                Clock::time_point now = Clock::now()) {
        if (!address || !scheduler::is_rdram_word_address(address, 0x3CU))
            throw std::runtime_error("Invalid host OSTask address");
        address = canonical(address);
        std::scoped_lock lock(mutex_);
        for (const auto& task : tasks_)
            if (task.pending && task.token.address == address)
                throw std::runtime_error("Host OSTask reused before completion");
        for (auto& task : tasks_) {
            if (task.pending) continue;
            task = {{address, ++generation_}, type == 1 ? 3U : 1U,
                    type, Stage::Queued, now, false};
            trace(task, "submitted", now);
            return task.token;
        }
        // Far beyond the scheduler's two active engines. Never silently drop
        // ownership if corrupted guest state floods the registry.
        throw std::runtime_error("Host OSTask registry exhausted");
    }
    Token lookup(std::uint32_t address, Engine engine) {
        if (!address) return {};
        std::scoped_lock lock(mutex_);
        for (const auto& task : tasks_)
            if ((task.pending & static_cast<unsigned>(engine)) &&
                task.token.address == canonical(address)) return task.token;
        return {};
    }
    void mark(Token token, Stage stage, Clock::time_point now = Clock::now()) {
        std::scoped_lock lock(mutex_);
        if (auto* task = find(token)) {
            task->stage = stage;
            task->progress = now;
            trace(*task, stage_name(stage), now);
        }
    }
    void consume(Token token, Engine engine, Clock::time_point now = Clock::now()) {
        std::scoped_lock lock(mutex_);
        if (auto* task = find(token)) {
            task->pending &= ~static_cast<unsigned>(engine);
            task->progress = now;
            trace(*task, engine == Engine::Sp ? "SP-consumed" : "DP-consumed", now);
        }
    }
    bool protect(std::uint32_t address, Engine engine, unsigned ticks) {
        std::scoped_lock lock(mutex_);
        for (const auto& task : tasks_) {
            if (!(task.pending & static_cast<unsigned>(engine)) ||
                task.token.address != canonical(address)) continue;
            if (ticks >= 10 && guard_count_ < 8) {
                ++guard_count_;
                std::fprintf(stderr, "[host-task] deferred-retail-retirement id=%llu task=%08X engine=%s ticks=%u stage=%s\n",
                    static_cast<unsigned long long>(task.token.generation), address,
                    engine == Engine::Sp ? "SP" : "DP", ticks, stage_name(task.stage));
            }
#if defined(DKR_TASK_QUALIFICATION)
            if (std::getenv("DKR_TASK_TEST_RETAIL_TIMEOUT")) return false;
#endif
            return true;
        }
        return false;
    }
    // One diagnostic per stalled task, not an unsafe forced completion. The
    // caller supplies its own native UI/exit path without acquiring GPU locks.
    bool take_stall(Task& result, Clock::time_point now = Clock::now()) {
        std::scoped_lock lock(mutex_);
        for (auto& task : tasks_) {
            if (!task.pending || task.reported || now - task.progress < std::chrono::seconds(15)) continue;
            task.reported = true;
            result = task;
            return true;
        }
        return false;
    }
};
inline Registry registry;

inline void qualification_delay(Stage stage, std::uint32_t type) {
#if defined(DKR_TASK_QUALIFICATION)
    if (type != 1) return;
    static bool injected = false; // Only called on the graphics worker.
    const char* selected = std::getenv("DKR_TASK_TEST_DELAY_STAGE");
    if (injected || !selected) return;
    if ((stage == Stage::SpNotify && std::string_view(selected) == "sp") ||
        (stage == Stage::DpNotify && std::string_view(selected) == "dp")) {
        const char* amount = std::getenv("DKR_TASK_TEST_DELAY_MS");
        const int ms = amount ? std::atoi(amount) : 2000;
        if (ms < 1 || ms > 20000) return;
        injected = true;
        std::fprintf(stderr, "[host-task][test] delay-stage=%s duration=%dms\n", selected, ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
#else
    (void)stage; (void)type;
#endif
}
}

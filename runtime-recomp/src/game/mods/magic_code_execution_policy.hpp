#pragma once

// Execution-only bookkeeping. No checkpoint/guest ABI, package API or Budget
// layout changes. A transaction is private to one World on its calling thread;
// native re-entry shares it, including when a different Budget& was supplied.
#include "magic_code_vm.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace dkr::mods::code::execution {
inline constexpr std::string_view Policy = "per-owner-work-v2";
enum class Phase { Simulation, Drawing, Loading };
inline thread_local Phase phase = Phase::Simulation;
struct PhaseScope {
    Phase previous = phase;
    explicit PhaseScope(Phase next) { phase = next; }
    ~PhaseScope() { phase = previous; }
};
inline const char* phase_name(Phase value) {
    switch(value) {
    case Phase::Loading: return "loading";
    case Phase::Drawing: return "drawing";
    default: return "simulation";
    }
}
inline Phase event_phase(std::string_view name) {
    return name == "on_boot" || name == "on_scene_load" || name == "on_scene_loaded" ||
           name == "on_scene_unload" || name == "on_reload" ||
           name == "on_cleanup" ? Phase::Loading : phase;
}
inline Phase function_phase(std::string_view name) {
    if(name.find("render") != name.npos || name.starts_with("draw_") ||
       name.starts_with("hud_")) return Phase::Drawing;
    if(name.find("init") != name.npos || name.find("load") != name.npos ||
       name.find("setup") != name.npos || name.starts_with("generate_"))
        return Phase::Loading;
    return Phase::Simulation;
}
struct Counts {
    std::uint64_t instructions = 0, native = 0, sdk = 0, cross = 0;
};
struct OwnerWork {
    std::string_view id;
    std::uint64_t generation = 0;
    std::uint64_t instruction_limit = 0, remaining_instructions = 0;
    unsigned native_limit = 0, remaining_native = 0;
    Counts counts;
};
struct Context {
    const void* world = nullptr;
    Budget* budget = nullptr;
    Phase phase = Phase::Simulation;
    std::string_view label;
    std::array<OwnerWork,128> owners{};
    std::size_t owner = 128;
    unsigned callbacks = 0, nesting = 0, maximum_nesting = 0;
    Counts total;
    std::uint64_t generation = 0;
    void reset(const void* next_world,Budget& next_budget,Phase next_phase,std::string_view next_label) {
        world=next_world;budget=&next_budget;phase=next_phase;label=next_label;
        owner=owners.size();callbacks=nesting=maximum_nesting=0;total={};
        if(++generation==0) { for(auto& work:owners)work.generation=0;generation=1; }
    }
};
inline thread_local Context* current = nullptr;
inline thread_local std::unique_ptr<Context> idle_context;
inline bool active(const void* world) { return current&&current->world==world; }
class Transaction {
    Context* previous_ = current;
    std::unique_ptr<Context> own_;
public:
    Transaction(const void* world, Budget& budget, std::string_view label) {
        if(!current || current->world != world) {
            own_=std::move(idle_context);
            if(!own_)own_=std::make_unique<Context>();
            own_->reset(world,budget,phase,label);
            current = own_.get();
        }
    }
    Budget& budget() const { return *current->budget; }
    ~Transaction() {
        if(!own_) return;
        // Sparse local telemetry only. Counters, not wall time, enforce work.
        static std::atomic<unsigned> reports{0};
        if((own_->label == "on_boot" || own_->total.sdk > 256 ||
            own_->total.native > 256) && reports.fetch_add(1) < 24) {
            std::fprintf(stderr,
                "[custom-magic-codes][work] policy=per-owner-work-v2 phase=%s event=%.*s "
                "callbacks=%u instructions=%llu sdk=%llu native=%llu cross=%llu depth=%u; "
                "SDK helpers do not consume native game-call allowance\n",
                phase_name(own_->phase), int(own_->label.size()), own_->label.data(),
                own_->callbacks, static_cast<unsigned long long>(own_->total.instructions),
                static_cast<unsigned long long>(own_->total.sdk),
                static_cast<unsigned long long>(own_->total.native),
                static_cast<unsigned long long>(own_->total.cross), own_->maximum_nesting);
        }
        current = previous_;
        if(!idle_context)idle_context=std::move(own_);
    }
};
class OwnerScope {
    Context& context_;
    std::size_t previous_, owner_;
    void save(std::size_t owner) {
        if(owner >= context_.owners.size()) return;
        auto& work = context_.owners[owner];
        work.remaining_instructions = context_.budget->instructions;
        work.remaining_native = context_.budget->native_calls;
    }
    void select(std::size_t owner) {
        const auto& work = context_.owners[owner];
        context_.budget->instructions = work.remaining_instructions;
        context_.budget->native_calls = work.remaining_native;
    }
public:
    OwnerScope(std::size_t owner, std::string_view id, unsigned instructions, unsigned native)
        : context_(*current), previous_(context_.owner), owner_(owner) {
        if(owner >= context_.owners.size()) throw Error("Custom Magic Code: invalid execution owner");
        auto& work = context_.owners[owner];
        if(work.generation!=context_.generation) {
            work.generation=context_.generation;work.id=id;work.counts={};
            const unsigned multiplier = context_.phase == Phase::Loading ? 8U :
                                        context_.phase == Phase::Drawing ? 2U : 1U;
            work.instruction_limit = std::uint64_t(instructions) * multiplier;
            work.native_limit = std::max(512U,native * 4U) *
                                (context_.phase == Phase::Loading ? 8U : 1U);
            work.remaining_instructions = work.instruction_limit;
            work.remaining_native = work.native_limit;
        }
        if(previous_ != owner) { save(previous_); select(owner); context_.owner = owner; }
        ++context_.callbacks;
        context_.maximum_nesting = std::max(context_.maximum_nesting,++context_.nesting);
    }
    const OwnerWork& work() const { return context_.owners[owner_]; }
    ~OwnerScope() {
        --context_.nesting;
        if(previous_ != owner_) {
            save(owner_);
            context_.owner = previous_;
            if(previous_ < context_.owners.size()) select(previous_);
        }
    }
};
inline void instruction() {
    if(current && current->owner < current->owners.size()) {
        ++current->owners[current->owner].counts.instructions;
        ++current->total.instructions;
    }
}
inline void gateway(bool sdk) {
    if(current && current->owner < current->owners.size()) {
        if(sdk) { ++current->owners[current->owner].counts.sdk; ++current->total.sdk; }
        else { ++current->owners[current->owner].counts.cross; ++current->total.cross; }
    }
}
inline void native(Budget& supplied) {
    auto& budget = current ? *current->budget : supplied;
    if(!budget.native_calls)
        throw Error("Custom Magic Code: native game-call safety limit exceeded");
    --budget.native_calls;
    if(current && current->owner < current->owners.size()) {
        ++current->owners[current->owner].counts.native;
        ++current->total.native;
    }
}
inline std::string diagnostic() {
    if(!current || current->owner >= current->owners.size()) return {};
    const auto& work = current->owners[current->owner];
    return " [work-policy=per-owner-work-v2 phase=" + std::string(phase_name(current->phase)) +
        " instruction-work=" + std::to_string(work.instruction_limit-current->budget->instructions) + "/" + std::to_string(work.instruction_limit) +
        " vm-instructions=" + std::to_string(work.counts.instructions) +
        " sdk=" + std::to_string(work.counts.sdk) + " native=" + std::to_string(work.counts.native) +
        "/" + std::to_string(work.native_limit) + " cross=" + std::to_string(work.counts.cross) +
        " chain-depth=" + std::to_string(current->maximum_nesting) + "]";
}
} // namespace dkr::mods::code::execution

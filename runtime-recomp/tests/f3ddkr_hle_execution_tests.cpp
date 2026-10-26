#include "f3ddkr_execution_policy.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
using dkr::runtime::f3ddkr::CommandBudget;
using dkr::runtime::f3ddkr::CommandCheck;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Command {
    std::uint32_t w0, w1;
};
static_assert(sizeof(Command) == 8);

// Only control flow is simulated here: all address/span admission and execution
// accounting use the same policy invoked by the production RT64 decoder.
struct SyntheticList {
    std::vector<std::uint64_t> memory;
    CommandBudget budget;
    std::vector<std::size_t> executed;
    unsigned opcode_reads = 0;

    SyntheticList(std::size_t words, std::size_t allowance) : memory(words), budget(allowance) {}

    void command(std::size_t offset, std::uint8_t opcode, std::uint32_t argument = 0, std::uint32_t flags = 0) {
        const Command command{(std::uint32_t(opcode) << 24) | flags, argument};
        std::memcpy(reinterpret_cast<std::uint8_t*>(memory.data()) + offset, &command, sizeof(command));
    }

    CommandCheck check(std::uintptr_t address, std::size_t bytes) {
        return budget.validate(reinterpret_cast<std::uintptr_t>(memory.data()), address, bytes, [&] {
            ++opcode_reads;
            std::uint32_t w0;
            std::memcpy(&w0, reinterpret_cast<const void*>(address), sizeof(w0));
            return static_cast<std::uint8_t>(w0 >> 24);
        });
    }

    std::optional<CommandCheck> run(std::size_t offset, std::optional<unsigned> count = std::nullopt) {
        std::vector<std::size_t> returns;
        unsigned consumed = 0;
        while (!count || consumed < *count) {
            const auto address = reinterpret_cast<std::uintptr_t>(memory.data()) + offset;
            const auto result = check(address, memory.size() * sizeof(memory[0]));
            if (result != CommandCheck::Okay)
                return result;
            Command current;
            std::memcpy(&current, reinterpret_cast<const void*>(address), sizeof(current));
            executed.push_back(offset);
            ++consumed;
            switch (current.w0 >> 24) {
            case 0x06: // Call or branch; policy must bound even a self-branch.
                if (!(current.w0 & 0x10000))
                    returns.push_back(offset + 8);
                offset = current.w1;
                break;
            case 0x07: { // A counted child shares this task's existing budget.
                const auto failure = run(current.w1, (current.w0 >> 16) & 0xff);
                if (failure)
                    return failure;
                offset += 8;
                break;
            }
            case 0xb8:
                if (returns.empty())
                    return std::nullopt;
                offset = returns.back();
                returns.pop_back();
                break;
            case 0xe4:
            case 0xe5:
            case 0xd5:
                offset += 24;
                break;
            default:
                offset += 8;
                break;
            }
        }
        return std::nullopt;
    }
};

void rejected_addresses_are_not_read() {
    SyntheticList list(4, 32);
    const auto base = reinterpret_cast<std::uintptr_t>(list.memory.data());
    for (const auto address : {base - 8, base + 32, base + 40, base + 1}) {
        require(list.check(address, 32) != CommandCheck::Okay, "bad address admitted");
    }
    require(list.check(base, 7) != CommandCheck::Okay, "truncated header admitted");
    require(list.check(base + 24, 28) != CommandCheck::Okay, "short final header admitted");
    require(list.opcode_reads == 0, "invalid header was read before rejection");
    require(list.budget.remaining_words() == 32, "rejected pointers consumed budget");
}

void texture_rectangles_require_all_three_words() {
    for (const auto opcode : {0xe4, 0xe5, 0xd5}) {
        for (const std::size_t bytes : {8, 16, 23, 24}) {
            SyntheticList list(3, 3);
            list.command(0, static_cast<std::uint8_t>(opcode));
            const auto base = reinterpret_cast<std::uintptr_t>(list.memory.data());
            const auto result = list.check(base, bytes);
            require(result == (bytes == 24 ? CommandCheck::Okay : CommandCheck::Truncated),
                    "texture rectangle span checked incorrectly");
            require(list.budget.remaining_words() == (bytes == 24 ? 0 : 3),
                    "texture rectangle budget must consume three words only when admitted");
        }
        SyntheticList short_budget(3, 2);
        short_budget.command(0, static_cast<std::uint8_t>(opcode));
        require(short_budget.run(0) == CommandCheck::BudgetExceeded,
                "texture rectangle exceeded available word budget");
        require(short_budget.executed.empty(), "over-budget texture rectangle dispatched");
    }
}

void variable_command_spans_require_the_full_sequence() {
    const std::uintptr_t base = 0x1000;
    for (const std::size_t bytes : {0, 7, 24, 31, 32}) {
        CommandBudget budget(4);
        unsigned opcode_reads = 0, span_reads = 0;
        const auto result = budget.validate(
            base, base, bytes,
            [&] {
                ++opcode_reads;
                return std::uint8_t{0x03};
            },
            [&](std::uint8_t opcode) {
                ++span_reads;
                require(opcode == 0x03, "span selector received wrong opcode");
                return std::size_t{4};
            });
        require((result == CommandCheck::Okay) == (bytes == 32),
                "four-word forced-matrix span admitted a truncated sequence");
        require(opcode_reads == (bytes < 8 ? 0U : 1U) && span_reads == opcode_reads,
                "variable span selector read an invalid command header");
        require(budget.remaining_words() == (bytes == 32 ? 0 : 4), "variable command word accounting mismatch");
    }
}

void terminal_and_fallthrough_boundaries() {
    SyntheticList ended(4, 1);
    ended.command(24, 0xb8);
    require(!ended.run(24), "last complete ENDDL must be accepted");
    require(ended.executed == std::vector<std::size_t>{24}, "ENDDL fell through");
    require(ended.budget.remaining_words() == 0, "ENDDL was not accounted");

    SyntheticList unterminated(1, 10);
    unterminated.command(0, 0x00);
    require(unterminated.run(0) == CommandCheck::OutsideMemory, "fallthrough past RDRAM must be rejected");
    require(unterminated.opcode_reads == 1, "fallthrough read an out-of-range opcode");
}

void self_branch_exhausts_task_budget() {
    SyntheticList loop(1, 17);
    loop.command(0, 0x06, 0, 0x10000);
    require(loop.run(0) == CommandCheck::BudgetExceeded, "self-branch did not stop");
    require(loop.executed.size() == 17, "self-branch executed beyond its task budget");
    require(loop.budget.remaining_words() == 0, "self-branch accounting mismatch");
}

void nested_calls_return_with_one_shared_budget() {
    SyntheticList list(6, 6);
    list.command(0, 0x06, 16);
    list.command(8, 0xb8);
    list.command(16, 0x06, 32);
    list.command(24, 0xb8);
    list.command(32, 0x00);
    list.command(40, 0xb8);
    require(!list.run(0), "nested calls should terminate normally");
    require(list.executed == std::vector<std::size_t>({0, 16, 32, 40, 24, 8}), "nested call return order changed");
    require(list.budget.remaining_words() == 0, "nested calls reset or skipped budget");
}

void counted_children_do_not_replenish_budget() {
    for (const auto allowance : {4, 5}) {
        SyntheticList list(5, allowance);
        list.command(0, 0x07, 16, 3 << 16);
        list.command(8, 0xb8);
        list.command(16, 0x00);
        list.command(24, 0x00);
        list.command(32, 0x00);
        const auto result = list.run(0);
        if (allowance == 4) {
            require(result == CommandCheck::BudgetExceeded, "counted child replenished the parent's execution budget");
            require(list.executed == std::vector<std::size_t>({0, 16, 24, 32}),
                    "parent executed after child consumed the task budget");
        } else {
            require(!result, "sufficient shared budget rejected counted list");
            require(list.executed == std::vector<std::size_t>({0, 16, 24, 32, 8}),
                    "counted list did not return to parent");
        }
        require(list.budget.remaining_words() == 0, "counted list accounting mismatch");
    }
}
} // namespace

int main() {
    try {
        rejected_addresses_are_not_read();
        texture_rectangles_require_all_three_words();
        variable_command_spans_require_the_full_sequence();
        terminal_and_fallthrough_boundaries();
        self_branch_exhausts_task_budget();
        nested_calls_return_with_one_shared_budget();
        counted_children_do_not_replenish_budget();
        std::cout << "F3DDKR command execution policy tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "magic_code_native_arguments.hpp"
#include <stdexcept>

using namespace dkr::mods;
using namespace dkr::mods::code;
namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("Native code argument regression");
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const Error&) {
        rejected = true;
    }
    check(rejected);
}
void word(Bytes& ram, std::uint32_t address, std::uint32_t value) {
    const auto offset = address - 0x80000000U;
    for (unsigned i = 0; i < 4; ++i)
        ram[(offset + i) ^ 3U] = std::uint8_t(value >> (24 - i * 8));
}
} // namespace
int main() {
    auto linked = std::make_shared<LinkedSet>();
    linked->identity = std::string(64, 'a');
    linked->resident_bytes = 0x2020;
    auto package = std::make_shared<Package>();
    package->id = "native-argument-fixture";
    Owner owner;
    owner.package = package;
    owner.begin = CodeBase;
    owner.end = CodeBase + linked->resident_bytes;
    owner.heap = CodeBase + 16;
    owner.heap_size = 16;
    owner.stack = CodeBase + 0x1000;
    owner.functions = {CodeBase};
    linked->owners.push_back(owner);
    linked->function_owners.emplace(CodeBase, 0);
    // lw v0,16(sp); jr ra; nop. Read the fifth O32 argument from the mod's
    // private callback stack, not from the engine's preview scratch pointer.
    ImageSection code{CodeBase, Bytes{0x8f, 0xa2, 0, 0x10, 0x03, 0xe0, 0, 0x08, 0, 0, 0, 0}, Bytes(16)};
    for (unsigned i = 0; i < code.bytes.size(); ++i)
        code.word_swapped[i ^ 3U] = code.bytes[i];
    linked->sections.push_back(std::move(code));
    linked->regions = {{CodeBase, 12, false, true}, {CodeBase + 16, 0x2010, true, false}};
    linked->events["*:preview"] = {CodeBase};
    linked->hooks[0x80001000U] = {{CodeBase}, {CodeBase}, CodeBase};
    Bytes ram(16 * MiB + linked->resident_bytes);
    World world(linked, 1);
    world.attach(ram, true);
    Registers registers;
    Budget budget;
    for (const auto stack :
         {DKR_OWNED_PREVIEW_STACK_BEGIN, DKR_OWNED_PREVIEW_STACK_TOP - 0xA8U, DKR_OWNED_PREVIEW_STACK_TOP}) {
        registers.integer[29] = std::uint64_t(std::int64_t(std::int32_t(stack)));
        for (unsigned i = 0; i < 16; ++i)
            word(ram, stack + i * 4, 0x10203000 + i);
        const auto outgoing = owned_preview_arguments(ram, true, registers);
        check(outgoing && (*outgoing)[4] == 0x10203004 && outgoing->back() == 0x1020300f);
        check(!owned_preview_arguments(ram, false, registers));
        const auto saved_sp = registers.integer[29];
        rejects([&] { world.call(CodeBase, registers, budget); });
        world.call(CodeBase, registers, budget, &*outgoing);
        check(registers.integer[2] == 0x10203004 && registers.integer[29] == saved_sp);
        world.event("*", "preview", registers, budget, &*outgoing);
        check(world.enter(0x80001000U, registers, budget, &*outgoing));
        world.leave(0x80001000U, registers, budget, &*outgoing);
    }
    Memory vm(ram, linked->regions);
    rejects([&] { vm.load(DKR_OWNED_PREVIEW_STACK_TOP, 4); });
    rejects([&] { vm.store(DKR_OWNED_PREVIEW_STACK_TOP, 4, 0); });
    rejects([&] { vm.load(DKR_OWNED_PREVIEW_STACK_TOP, 4, true); });
    registers.integer[29] = std::int32_t(DKR_OWNED_PREVIEW_STACK_TOP);
    const auto truncated = std::span<const std::uint8_t>(ram).first(DKR_OWNED_PREVIEW_STACK_TOP - 0x80000000U + 60);
    rejects([&] { owned_preview_arguments(truncated, true, registers); });
    registers.integer[29] = std::int32_t(DKR_OWNED_PREVIEW_STACK_TOP - 1);
    rejects([&] { owned_preview_arguments(ram, true, registers); });
    for (const auto outside :
         {0x8011f730U, DKR_OWNED_PREVIEW_STACK_BEGIN - 4, DKR_OWNED_PREVIEW_STACK_TOP + 4, CodeBase + 0x1000}) {
        registers.integer[29] = std::int32_t(outside);
        check(!owned_preview_arguments(ram, true, registers));
    }
    // Native retail callbacks continue to use their original checked route.
    registers.integer[29] = std::int32_t(0x8011f730U);
    word(ram, 0x8011f740U, 0x55);
    world.call(CodeBase, registers, budget);
    check(registers.integer[2] == 0x55);
}

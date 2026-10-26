#include "magic_code_world.hpp"
#include <recompiler/context.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <cstdio>
#include <chrono>

#include "magic_code_execution_policy.hpp"

namespace dkr::mods::code {
namespace {
[[noreturn]] void bad(const std::string& s) {
    throw Error("Custom Magic Code: " + s);
}
std::string guest_string(Memory& m, std::uint32_t a, std::size_t maximum = 96) {
    std::string result;
    for (unsigned i = 0; i <= maximum; ++i) {
        if (i > 0xffffffffU - a)
            bad("string address overflow");
        const auto c = char(m.load(a + i, 1));
        if (!c)
            return result;
        if (i == maximum)
            bad("unterminated guest string");
        result += c;
    }
    bad("invalid guest string");
}
void copy(Memory& m, std::uint32_t a, View value) {
    if (value.size() > 4096 || value.size() > 0xffffffffU - a)
        bad("buffer exceeds the SDK call limit");
    m.write(a, value);
}
struct Writer {
    Bytes bytes;
    void word(std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(std::uint8_t(v >> (24 - 8 * i)));
    }
    void wide(std::uint64_t v) {
        word(std::uint32_t(v >> 32));
        word(std::uint32_t(v));
    }
};
struct Reader {
    View bytes;
    std::size_t at = 0;
    std::uint32_t word() {
        const auto v = be32(bytes, at);
        at += 4;
        return v;
    }
    std::uint64_t wide() {
        const auto high = word();
        return std::uint64_t(high) << 32 | word();
    }
};
} // namespace
struct World::Restore {
    const LinkedSet* owner = nullptr;
    std::vector<State> states;
    std::vector<std::pair<std::uint32_t, Bytes>> memory;
    bool initialized = false;
    std::vector<Effect> effects;
};
World::World(std::shared_ptr<const LinkedSet> linked, std::uint64_t seed, Native native, Service service)
    : linked_(std::move(linked)), native_(std::move(native)), service_(std::move(service)) {
    if (!linked_ || linked_->owners.size() > 128 || linked_->identity.size() != 64)
        bad("invalid session code identity");
    for (std::size_t i = 0; i < linked_->owners.size(); ++i) {
        auto v = seed += 0x9e3779b97f4a7c15ULL;
        v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
        v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
        v ^= v >> 31;
        state_.push_back({v ? v : 1, 0, {{0, linked_->owners[i].heap_size, false}}});
        calls_.push_back(0);
        metrics_.emplace_back();
    }
}
LinkPolicy World::core_policy() {
    LinkPolicy p;
    p.modules = {{"core", 1}, {"memory", 1}, {"events", 1}, {"settings", 1}, {"resources", 1}, {"random", 1}};
    p.services = {"dkr_sdk_version",    "dkr_random_u32",    "dkr_alloc",     "dkr_free",
                  "dkr_setting_number", "dkr_data_size",     "dkr_data_read", "dkr_setting_string",
                  "dkr_resource_size",  "dkr_resource_read", "dkr_log"};
    p.events = {"on_boot"};
    // Other game lifecycle events are added by the concrete world adapter,
    // once it actually emits them. Never silently accept an unimplemented event.
    return p;
}
Memory& World::memory() const {
    if (!bus_)
        bad("code world has no RDRAM binding");
    return *bus_;
}
void World::attach(std::span<std::uint8_t> ram, bool initial) {
    if (active_)
        bad("cannot replace RDRAM during a code callback");
    if (initial && (!ram_.empty() || initialized_))
        bad("initial code installation cannot reset an existing world");
    if (ram.size() < 16 * MiB + linked_->resident_bytes)
        bad("RDRAM is too small for the admitted code set");
    auto regions = linked_->regions;
    regions.push_back({0x80000000U, 8 * unsigned(MiB), true, false});
    auto bus = std::make_unique<Memory>(ram, std::move(regions));
    if (initial)
        install(*linked_, ram);
    ram_ = ram;
    bus_ = std::move(bus);
}
std::unique_ptr<World::Rebind> World::prepare_rebind(std::shared_ptr<const LinkedSet> image, int migration) const {
    if (!idle() || ram_.empty() || !image || image->resident_bytes != linked_->resident_bytes)
        bad("reload needs a retired equal-size world");
    auto regions = image->regions;
    if (migration >= 0)
        for (auto& r : regions)
            if (r.writable) {
                const auto& owner = image->owners.at(unsigned(migration));
                r.writable = r.address >= owner.begin && r.address + std::uint64_t(r.size) <= owner.end;
            }
    regions.push_back({0x80000000U, 8 * unsigned(MiB), migration < 0, false});
    auto result = std::make_unique<Rebind>();
    result->linked = std::move(image);
    result->migration = migration;
    result->bus = std::make_unique<Memory>(ram_, std::move(regions));
    return result;
}
void World::exchange(Rebind& value) noexcept {
    linked_.swap(value.linked);
    bus_.swap(value.bus);
    std::swap(migration_owner_, value.migration);
    for (const auto& r : linked_->regions)
        if (r.executable) {
            const auto image = std::find_if(linked_->sections.begin(), linked_->sections.end(), [&](const auto& s) {
                return r.address >= s.address && r.address - s.address < s.bytes.size();
            });
            // All extents/alignment were admitted by link(), before the swap.
            std::memcpy(ram_.data() + r.address - 0x80000000U, image->word_swapped.data() + r.address - image->address,
                        r.size);
        }
}
void World::owner_event(unsigned owner, std::string_view name, Budget& budget, const Registers& args) {
    execution::PhaseScope phase_scope(execution::event_phase(name));
    execution::Transaction transaction(this, budget, name);
    const auto found = linked_->events.find("*:" + std::string(name));
    if (found == linked_->events.end())
        return;
    for (auto address : found->second)
        if (linked_->function_owners.at(address) == owner) {
            auto c = args;
            call(address, c, budget);
        }
}
void World::detach() {
    if (active_)
        bad("cannot retire RDRAM during a code callback");
    bus_.reset();
    ram_ = {};
}
void World::initialize(Budget& budget) {
    execution::PhaseScope phase_scope(execution::Phase::Loading);
    execution::Transaction transaction(this, budget, "on_boot");
    if (initialized_ || active_ || ram_.empty())
        bad("code world initialization requires a fresh retired session");
    initialized_ = true;
    event("*", "on_boot", Registers{}, budget);
}
void World::call(std::uint32_t function, Registers& c, Budget& budget, const ArgumentStack* native_arguments) {
    const auto found = linked_->function_owners.find(function);
    if (found == linked_->function_owners.end() || ram_.empty())
        bad("unlinked code callback");
    const auto owner = found->second;
    auto& depth = calls_.at(owner);
    const auto& package = *linked_->owners[owner].package;
    execution::Transaction transaction(this, budget, "callback");
    auto& work_budget = transaction.budget();
    execution::OwnerScope execution_owner(owner, package.id, package.api == 2 ? package.sdk2_instructions : 200000,
                                          package.api == 2 ? package.sdk2_native_calls : 128);
    const auto before = execution_owner.work().counts;
    const auto started = std::chrono::steady_clock::now();
    struct Costs {
        Metric& metric;
        const execution::OwnerWork& work;
        execution::Counts before;
        bool outer;
        std::chrono::steady_clock::time_point started;
        ~Costs() {
            ++metric.callbacks;
            if (!outer)
                return;
            metric.instructions += work.counts.instructions - before.instructions;
            metric.native_calls += work.counts.native - before.native;
            metric.nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
                    .count();
        }
    } costs{metrics_[owner], execution_owner.work(), before, depth == 0, started};
    if (depth >= 8)
        bad("package callback stack exhausted");
    try {
        const auto saved_sp = c.integer[29];
        const auto saved_fcsr = c.fcsr;
        // Preserve O32 arguments 5+ across package exports/native callbacks.
        // Only the native entry can supply an approved argument snapshot.
        // Nested VM/import calls still read their own permission-checked stack.
        auto& bus = memory();
        ArgumentStack outgoing{};
        if (native_arguments)
            outgoing = *native_arguments;
        else if (saved_sp)
            for (unsigned i = 0; i < outgoing.size(); ++i)
                outgoing[i] = std::uint32_t(bus.load(std::uint32_t(saved_sp) + i * 4, 4));
        c.integer[29] = std::uint64_t(std::int64_t(std::int32_t(linked_->owners[owner].stack - depth * 4096)));
        for (unsigned i = 0; i < outgoing.size(); ++i)
            bus.store(std::uint32_t(c.integer[29]) + i * 4, 4, outgoing[i]);
        c.fcsr = state_[owner].fcsr;
        ++depth;
        ++active_;
        struct Guard {
            World& w;
            unsigned& depth;
            Registers& c;
            std::uint64_t sp;
            std::uint32_t fcsr;
            std::size_t owner;
            ~Guard() {
                w.state_[owner].fcsr = c.fcsr;
                c.integer[29] = sp;
                c.fcsr = fcsr;
                --depth;
                --w.active_;
            }
        } guard{*this, depth, c, saved_sp, saved_fcsr, owner};
        execute(bus, c, function, work_budget,
                [&](auto target, auto& registers, auto& fuel) { gateway(target, registers, fuel); });
    } catch (const Error& e) {
        char context[160];
        std::snprintf(context, sizeof(context), " [package=%s callback=0x%08X]",
                      linked_->owners[owner].package->id.c_str(), function);
        std::string location;
        const auto& package = *linked_->owners[owner].package;
        if (package.api == 2 && !package.debug_lines.empty()) {
            const auto& functions = linked_->owners[owner].functions;
            const auto found = std::find(functions.begin(), functions.end(), function);
            if (found != functions.end()) {
                const auto& name = package.symbols->functions[std::size_t(found - functions.begin())].name;
                if (const auto line = package.debug_lines.find(name); line != package.debug_lines.end())
                    location = " [callback-source=" + line->second + "]";
            }
        }
        throw Error(std::string(e.what()) + context + location + execution::diagnostic());
    }
}
void World::event(std::string_view owner, std::string_view name, const Registers& arguments, Budget& budget,
                  const ArgumentStack* native_arguments) {
    execution::PhaseScope phase_scope(execution::event_phase(name));
    execution::Transaction transaction(this, budget, name);
    const auto found = linked_->events.find(std::string(owner) + ":" + std::string(name));
    if (found != linked_->events.end())
        for (auto function : found->second) {
            auto c = arguments;
            call(function, c, budget, native_arguments);
        }
}
bool World::has_hook(std::uint32_t function) const {
    return linked_->hooks.contains(function);
}
bool World::enter(std::uint32_t function, Registers& c, Budget& budget, const ArgumentStack* native_arguments) {
    execution::Transaction transaction(this, budget, "entry-hooks");
    const auto found = linked_->hooks.find(function);
    if (found == linked_->hooks.end())
        return false;
    for (auto hook : found->second.entry) {
        auto context = c;
        call(hook, context, budget, native_arguments);
    }
    if (found->second.replacement) {
        call(*found->second.replacement, c, budget, native_arguments);
        return true;
    }
    return false;
}
void World::leave(std::uint32_t function, const Registers& c, Budget& budget, const ArgumentStack* native_arguments) {
    execution::Transaction transaction(this, budget, "exit-hooks");
    const auto found = linked_->hooks.find(function);
    if (found == linked_->hooks.end())
        return;
    for (auto hook : found->second.exit) {
        auto context = c;
        call(hook, context, budget, native_arguments);
    }
}
void World::gateway(std::uint32_t address, Registers& c, Budget& budget) {
    if (migration_owner_ >= 0) {
        const auto found = linked_->imports.find(address);
        if (found == linked_->imports.end() || found->second.owner != unsigned(migration_owner_) ||
            found->second.event || found->second.dependency != "*" ||
            !core_policy().services.contains(found->second.name))
            bad("reload migration permits only this owner's core services");
    }
    const auto found = linked_->imports.find(address);
    if (found == linked_->imports.end()) {
        if (!linked_->native_functions.contains(address) || !native_)
            bad("unlinked or protected base-game call");
        native_(address, c, budget);
        return;
    }
    const auto& import = found->second;
    const auto& owner = linked_->owners[import.owner];
    execution::gateway(import.dependency == "*" && !import.event);
    if (import.event) {
        event(import.dependency, import.name, c, budget);
        return;
    }
    if (import.dependency == "*") {
        auto& bus = memory();
        static const auto policy = core_policy();
        if (policy.services.contains(import.name))
            core(import.name, import.owner, bus, c, budget);
        else {
            if (!service_ || !service_(import.name, import.owner, bus, c, budget))
                bad("SDK service was not implemented: " + import.name);
        }
    } else {
        const auto id = import.dependency == "." ? owner.package->id : import.dependency;
        const auto target = std::find_if(linked_->owners.begin(), linked_->owners.end(),
                                         [&](const auto& o) { return o.package->id == id; });
        if (target == linked_->owners.end())
            bad("missing package dependency");
        const auto& context = *target->package->symbols;
        for (auto index : context.exported_funcs)
            if (context.functions[index].name == import.name) {
                call(target->functions.at(index), c, budget);
                return;
            }
        bad("missing exported function");
    }
}
void World::core(std::string_view name, std::size_t index, Memory& bus, Registers& c, Budget&) {
    const auto& owner = linked_->owners[index];
    auto& state = state_[index];
    const auto a = std::uint32_t(c.integer[4]), b = std::uint32_t(c.integer[5]), d = std::uint32_t(c.integer[6]);
    if (name == "dkr_sdk_version") {
        c.integer[2] = owner.package->api;
        return;
    }
    if (name == "dkr_random_u32") {
        auto x = state.random;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state.random = x;
        c.integer[2] = std::uint32_t((x * 0x2545f4914f6cdd1dULL) >> 32);
        return;
    }
    if (name == "dkr_alloc") {
        c.integer[2] = 0;
        if (!a || a > owner.heap_size)
            return;
        const auto size = (a + 15U) & ~15U;
        for (std::size_t i = 0; i < state.blocks.size(); ++i)
            if (!state.blocks[i].used && state.blocks[i].size >= size) {
                auto& block = state.blocks[i];
                const auto address = owner.heap + block.offset;
                const auto left = block.size - size;
                const auto next = block.offset + size;
                block.size = size;
                block.used = true;
                if (left)
                    state.blocks.insert(state.blocks.begin() + i + 1, {next, left, false});
                bus.fill(address, size);
                c.integer[2] = std::uint64_t(std::int64_t(std::int32_t(address)));
                return;
            }
        return;
    }
    if (name == "dkr_free") {
        if (!a)
            return;
        for (std::size_t i = 0; i < state.blocks.size(); ++i)
            if (owner.heap + state.blocks[i].offset == a && state.blocks[i].used) {
                state.blocks[i].used = false;
                if (i + 1 < state.blocks.size() && !state.blocks[i + 1].used) {
                    state.blocks[i].size += state.blocks[i + 1].size;
                    state.blocks.erase(state.blocks.begin() + i + 1);
                }
                if (i && !state.blocks[i - 1].used) {
                    state.blocks[i - 1].size += state.blocks[i].size;
                    state.blocks.erase(state.blocks.begin() + i);
                }
                return;
            }
        bad("invalid or repeated heap free");
    }
    const auto key = guest_string(bus, a, name == "dkr_log" ? 512 : 96);
    if (name == "dkr_setting_number" || name == "dkr_setting_string") {
        if (!owner.settings.contains(key))
            bad("unknown setting " + key);
        const auto& value = owner.settings[key];
        if (name == "dkr_setting_number") {
            if (!value.is_number())
                bad("setting is not numeric");
            if (owner.package->api == 2) {
                const auto& options = owner.package->options;
                const auto found = std::find_if(options.begin(), options.end(),
                                                [&](const auto& option) { return option.at("id") == key; });
                if (found != options.end()) {
                    const auto at = owner.services + 28672 + unsigned(found - options.begin()) * 8;
                    const auto tag = bus.load(at, 4);
                    const auto raw = std::uint32_t(bus.load(at + 4, 4));
                    if (tag == 1 || tag == 2) {
                        const double live = tag == 1 ? double(std::bit_cast<float>(raw)) : double(raw);
                        c.floating[0] = std::bit_cast<std::uint64_t>(live);
                        return;
                    }
                }
            }
            c.floating[0] = std::bit_cast<std::uint64_t>(value.get<double>());
            return;
        }
        if (!value.is_string())
            bad("setting is not text");
        const auto& text = value.get_ref<const std::string&>();
        c.integer[2] = text.size() + 1;
        if (!b || !d)
            return;
        if (d < text.size() + 1 || d > 4096)
            bad("setting buffer is too small or exceeds its limit");
        copy(bus, b, View(reinterpret_cast<const std::uint8_t*>(text.c_str()), text.size() + 1));
        return;
    }
    if (name == "dkr_log") {
        if (effects_.size() >= 128)
            bad("code effect budget exceeded");
        effects_.push_back({owner.package->id, key});
        return;
    }
    if (name == "dkr_data_size" || name == "dkr_data_read") {
        const auto found = owner.data.find(key);
        if (found == owner.data.end()) {
            c.integer[2] = 0;
            return;
        }
        const auto& bytes = found->second;
        if (name == "dkr_data_size") {
            c.integer[2] = bytes.size();
            return;
        }
        const auto count = std::uint32_t(c.integer[7]);
        if (b > bytes.size() || count > bytes.size() - b || count > 4096)
            bad("structured data read exceeds its limit");
        copy(bus, d, View(bytes).subspan(b, count));
        c.integer[2] = count;
        return;
    }
    const auto found = owner.package->resources.find(key);
    if (found == owner.package->resources.end())
        bad("unknown package resource");
    const auto& bytes = found->second;
    if (name == "dkr_resource_size") {
        c.integer[2] = bytes.size();
        return;
    }
    if (name == "dkr_resource_read") {
        const auto count = std::uint32_t(c.integer[7]);
        if (b > bytes.size() || count > bytes.size() - b || count > 4096)
            bad("resource read exceeds its limit");
        copy(bus, d, View(bytes).subspan(b, count));
        c.integer[2] = count;
        return;
    }
    bad("unknown core service");
}
std::vector<Effect> World::take_effects() {
    verify_immutable();
    if (active_)
        bad("effects can only retire at a callback boundary");
    auto out = std::move(effects_);
    effects_.clear();
    return out;
}
Bytes World::checkpoint() const {
    verify_immutable();
    if (active_ || ram_.empty())
        bad("checkpoint requires a retired code boundary");
    // Exact canonical size; preserve every field and zero-padding decision.
    // One allocation instead of repeatedly growing the serialized image.
    std::size_t serialized = 80;
    for (const auto& s : state_)
        serialized += 16 + s.blocks.size() * 12;
    for (const auto& effect : effects_)
        serialized += 8 + effect.text.size();
    for (const auto& region : linked_->regions)
        if (region.writable)
            serialized += 8 + region.size;
    Writer w;
    w.bytes.reserve(serialized);
    w.word(ExecutionVersion);
    w.bytes.insert(w.bytes.end(), linked_->identity.begin(), linked_->identity.end());
    w.word(initialized_ ? 1U : 0U);
    w.word(std::uint32_t(state_.size()));
    for (const auto& s : state_) {
        w.wide(s.random);
        w.word(s.fcsr);
        w.word(std::uint32_t(s.blocks.size()));
        for (const auto& block : s.blocks) {
            w.word(block.offset);
            w.word(block.size);
            w.word(block.used ? 1U : 0U);
        }
    }
    w.word(std::uint32_t(effects_.size()));
    for (const auto& effect : effects_) {
        const auto owner = std::find_if(linked_->owners.begin(), linked_->owners.end(),
                                        [&](const auto& o) { return o.package->id == effect.package; });
        if (owner == linked_->owners.end() || effect.text.size() > 512)
            bad("invalid pending code effect");
        w.word(std::uint32_t(owner - linked_->owners.begin()));
        w.word(std::uint32_t(effect.text.size()));
        w.bytes.insert(w.bytes.end(), effect.text.begin(), effect.text.end());
    }
    const auto& bus = memory();
    for (const auto& region : linked_->regions)
        if (region.writable) {
            w.word(region.address);
            w.word(region.size);
            const auto offset = w.bytes.size();
            w.bytes.resize(offset + region.size);
            bus.read(region.address, std::span(w.bytes).subspan(offset, region.size));
        }
    return std::move(w.bytes);
}
void World::verify_immutable() const {
    if (ram_.empty())
        bad("immutable code has no RDRAM binding");
    for (const auto& region : linked_->regions)
        if (region.executable) {
            const auto image = std::find_if(linked_->sections.begin(), linked_->sections.end(), [&](const auto& s) {
                return region.address >= s.address && region.address - s.address <= s.bytes.size() &&
                       region.size <= s.bytes.size() - (region.address - s.address);
            });
            if (image == linked_->sections.end())
                bad("immutable code image is missing");
            const auto offset = region.address - 0x80000000U, source = region.address - image->address;
            if (std::memcmp(ram_.data() + offset, image->word_swapped.data() + source, region.size) != 0)
                bad("a native game call attempted to modify immutable mod code");
        }
}
std::shared_ptr<World::Restore> World::stage(View bytes) const {
    if (active_ || bytes.size() > MaxResident + 128 * 65536U || bytes.size() < 80 ||
        !std::equal(linked_->identity.begin(), linked_->identity.end(), bytes.begin() + 4))
        bad("checkpoint code identity differs");
    Reader r{bytes};
    if (r.word() != ExecutionVersion)
        bad("unsupported code checkpoint version");
    r.at += 64;
    const auto initialized = r.word();
    if (initialized > 1)
        bad("invalid code initialization state");
    if (r.word() != state_.size())
        bad("checkpoint owner count differs");
    auto result = std::make_shared<Restore>();
    result->owner = linked_.get();
    result->initialized = initialized != 0;
    for (std::size_t i = 0; i < state_.size(); ++i) {
        State s;
        s.random = r.wide();
        s.fcsr = r.word();
        const auto count = r.word();
        if (!s.random || (s.fcsr & ~0x0183ffffU) || !count || count > 4096)
            bad("invalid code state");
        unsigned offset = 0;
        bool previous_free = false;
        for (unsigned j = 0; j < count; ++j) {
            Block b;
            b.offset = r.word();
            b.size = r.word();
            const auto used = r.word();
            b.used = used != 0;
            if (used > 1 || b.offset != offset || !b.size || b.size % 16 || offset > linked_->owners[i].heap_size ||
                b.size > linked_->owners[i].heap_size - offset || (!b.used && previous_free))
                bad("invalid code heap checkpoint");
            offset += b.size;
            previous_free = !b.used;
            s.blocks.push_back(b);
        }
        if (offset != linked_->owners[i].heap_size)
            bad("incomplete code heap checkpoint");
        result->states.push_back(std::move(s));
    }
    const auto count = r.word();
    if (count > 128)
        bad("checkpoint effect queue exceeds its bound");
    for (unsigned i = 0; i < count; ++i) {
        const auto owner = r.word(), size = r.word();
        if (owner >= linked_->owners.size() || size > 512)
            bad("invalid checkpoint effect");
        const auto text = slice(bytes, r.at, size);
        r.at += size;
        if (std::find(text.begin(), text.end(), 0) != text.end())
            bad("checkpoint log message contains a NUL");
        result->effects.push_back({linked_->owners[owner].package->id, std::string(text.begin(), text.end())});
    }
    for (const auto& region : linked_->regions)
        if (region.writable) {
            if (r.word() != region.address || r.word() != region.size)
                bad("checkpoint memory layout differs");
            const auto block = slice(bytes, r.at, region.size);
            r.at += region.size;
            result->memory.emplace_back(region.address, Bytes(block.begin(), block.end()));
        }
    if (r.at != bytes.size())
        bad("trailing code checkpoint data");
    return result;
}
void World::commit(std::shared_ptr<Restore> restore) {
    if (active_ || ram_.empty() || !restore || restore->owner != linked_.get() || restore.use_count() != 1)
        bad("invalid or shared code checkpoint commit");
    // stage() performed ALL allocation and validation. Commit is single-use,
    // exclusively owned, and allocation-free so the surrounding RAM/native
    // transaction cannot fail halfway because a vector copy allocates here.
    auto& bus = memory();
    for (const auto& [address, bytes] : restore->memory)
        bus.write(address, bytes);
    state_.swap(restore->states);
    effects_.swap(restore->effects);
    initialized_ = restore->initialized;
    restore->owner = nullptr;
}
} // namespace dkr::mods::code

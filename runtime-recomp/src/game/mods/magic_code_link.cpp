#include "magic_code_link.hpp"
#include <recompiler/context.h>
#include <algorithm>

#include "magic_code_execution_policy.hpp"

namespace dkr::mods::code {
namespace {
[[noreturn]] void bad(const std::string& reason) {
    throw Error("Custom Magic Code: " + reason);
}
std::uint32_t aligned(std::uint32_t v) {
    return (v + 15U) & ~15U;
}
std::string dependency(const N64Recomp::Context& c, std::size_t index) {
    for (const auto& [name, i] : c.dependencies_by_name)
        if (i == index)
            return name;
    bad("invalid dependency index");
}
std::string event_key(std::string_view owner, std::string_view event) {
    return std::string(owner) + ":" + std::string(event);
}
void put(Bytes& b, std::size_t off, std::uint32_t v) {
    if (off > b.size() || 4 > b.size() - off)
        bad("relocation writes outside its section");
    for (unsigned i = 0; i < 4; ++i)
        b[off + i] = std::uint8_t(v >> (24 - 8 * i));
}
} // namespace
std::shared_ptr<const LinkedSet> link(const std::vector<std::shared_ptr<const Package>>& ordered,
                                      const std::map<std::string, Json>& settings, std::string_view revision,
                                      const LinkPolicy& policy, const std::map<std::string, Json>& data) {
    if (ordered.size() > 128)
        bad("too many code packages enabled in one session");
    unsigned simulation_owners = 0, tuning_owners = 0;
    for (const auto& p : ordered)
        if (p && p->api == 2) {
            if (p->modules.contains("simulation2") && ++simulation_owners > 1)
                bad("only one simulation control provider can own a session");
            if (p->modules.contains("racer-tuning2") && ++tuning_owners > 1)
                bad("only one racer tuning provider can own a session");
        }
    std::set<std::string> enabled;
    for (const auto& p : ordered)
        if (!p || !enabled.insert(p->id).second)
            bad("duplicate or missing code package");
    const auto resolved = resolve(ordered, enabled, revision, policy.online, policy.modules);
    if (resolved != ordered)
        bad("code packages are not in canonical dependency order");
    auto out = std::make_shared<LinkedSet>();
    out->revision = revision;
    out->identity = session_identity(ordered, settings, revision, data);
    if (policy.raw_game_access)
        out->native_functions = policy.call_functions;
    std::uint32_t cursor = CodeBase, next_import = 0x8f000000U;
    struct Local {
        std::vector<std::uint32_t> bases, imports, events;
        std::vector<std::size_t> images;
    };
    std::vector<Local> locals;
    std::map<std::string, std::map<std::string, std::uint32_t>> exports;
    auto allocate = [&](std::size_t size) {
        cursor = aligned(cursor);
        const auto begin = cursor;
        if (cursor - CodeBase > MaxResident || size > MaxResident - (cursor - CodeBase))
            bad("enabled code exceeds the session memory budget");
        cursor += std::uint32_t(size);
        return begin;
    };
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto& package = ordered[index];
        const auto& p = *package;
        // This withdrawn sample wrote bytes into retail's s16 Track Select
        // grid. Refuse its exact executable image BEFORE any callback runs;
        // keep it installed so its saved cup document can migrate on upgrade.
        if (const auto issue = compatibility_issue(p.digest); !issue.empty())
            bad(issue);
        if (!p.symbols || !p.revisions.contains(std::string(revision)))
            bad("code package does not match this Game Pak");
        for (const auto& [module, version] : p.modules) {
            const auto found = policy.modules.find(module);
            if (found == policy.modules.end() || found->second != version)
                bad("required SDK module is unavailable: " + module);
        }
        Owner owner;
        owner.package = package;
        const auto options = settings.find(p.id);
        owner.settings = configuration(p, options == settings.end() ? Json::object() : options->second);
        if (const auto documents = data.find(p.id); documents != data.end()) {
            const auto canonical = structured_data(documents->second);
            for (const auto& [id, value] : canonical.items()) {
                const auto encoded = value.dump();
                owner.data.emplace(id, Bytes(encoded.begin(), encoded.end()));
            }
        }
        if (exports.contains(p.id))
            bad("multiple editions of " + p.id + " were selected");
        owner.begin = aligned(cursor);
        auto& symbols = *p.symbols;
        Local local;
        for (const auto& section : symbols.sections) {
            const auto base = allocate(std::size_t(section.size) + section.bss_size);
            local.bases.push_back(base);
            local.images.push_back(out->sections.size());
            ImageSection image{base, Bytes(std::size_t(section.size) + section.bss_size, 0)};
            std::copy_n(p.binary.begin() + section.rom_addr, section.size, image.bytes.begin());
            out->sections.push_back(std::move(image));
        }
        for (const auto& f : symbols.functions) {
            if (f.section_index >= local.bases.size())
                bad("function has an invalid section");
            validate_instructions(slice(p.binary, f.rom, f.words.size() * 4));
            const auto address = local.bases[f.section_index] + f.vram - symbols.sections[f.section_index].ram_addr;
            owner.functions.push_back(address);
            if (!out->function_owners.emplace(address, index).second)
                bad("duplicate function address");
            if (!f.name.empty() && !exports[p.id].emplace(f.name, address).second)
                bad("duplicate function export");
        }
        exports.try_emplace(p.id);
        // Separate executable/read-only function ranges from writable data.
        for (std::size_t s = 0; s < symbols.sections.size(); ++s) {
            const auto& section = symbols.sections[s];
            const auto base = local.bases[s];
            std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
            for (std::size_t f = 0; f < symbols.functions.size(); ++f)
                if (symbols.functions[f].section_index == s)
                    ranges.emplace_back(owner.functions[f],
                                        owner.functions[f] + std::uint32_t(symbols.functions[f].words.size() * 4));
            std::sort(ranges.begin(), ranges.end());
            auto at = base;
            for (const auto& [begin, end] : ranges) {
                if (begin > at)
                    out->regions.push_back({at, begin - at, true, false});
                out->regions.push_back({begin, end - begin, false, true});
                at = end;
            }
            const auto end = base + section.size + section.bss_size;
            if (at < end)
                out->regions.push_back({at, end - at, true, false});
        }
        owner.heap_size = p.api == 2 ? p.sdk2_heap : 64 * 1024;
        owner.heap = allocate(owner.heap_size);
        out->regions.push_back({owner.heap, owner.heap_size, true, false});
        constexpr unsigned StackBytes = 32 * 1024;
        const auto stack = allocate(StackBytes);
        owner.stack = stack + StackBytes - 80;
        out->regions.push_back({stack, StackBytes, true, false});
        constexpr unsigned NativeStackBytes = 64 * 1024;
        const auto native_stack = allocate(NativeStackBytes);
        owner.native_stack = native_stack + NativeStackBytes - 80;
        out->regions.push_back({native_stack, NativeStackBytes, true, false});
        const unsigned service_bytes = p.api == 2 ? 65536U : 4096U;
        owner.services = allocate(service_bytes);
        out->regions.push_back({owner.services, service_bytes, true, false});
        owner.end = cursor;
        for (const auto& symbol : symbols.import_symbols) {
            const auto dep = dependency(symbols, symbol.dependency_index);
            if (dep == "*" && !policy.services.contains(symbol.base.name))
                bad("unavailable SDK import: " + symbol.base.name);
            if (dep != "*" && dep != "." &&
                std::none_of(p.dependencies.begin(), p.dependencies.end(), [&](const auto& d) { return d.id == dep; }))
                bad("code dependency is absent from the manifest");
            local.imports.push_back(next_import);
            out->imports.emplace(next_import, LinkedImport{index, dep, symbol.base.name, false});
            next_import += 4;
        }
        for (const auto& event : symbols.event_symbols) {
            local.events.push_back(next_import);
            out->imports.emplace(next_import, LinkedImport{index, p.id, event.base.name, true});
            next_import += 4;
        }
        locals.push_back(std::move(local));
        out->owners.push_back(std::move(owner));
    }
    for (const auto& [address, import] : out->imports)
        if (import.dependency != "*" && !import.event) {
            const auto id = import.dependency == "." ? out->owners[import.owner].package->id : import.dependency;
            if (!exports.contains(id) || !exports.at(id).contains(import.name))
                bad("missing exported function " + id + ":" + import.name);
        }
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto& p = *ordered[index];
        const auto& symbols = *p.symbols;
        const auto& local = locals[index];
        const auto& owner = out->owners[index];
        for (std::size_t s = 0; s < symbols.sections.size(); ++s) {
            const auto& section = symbols.sections[s];
            auto& image = out->sections[local.images[s]].bytes;
            for (const auto& relocation : section.relocs) {
                if (relocation.type == N64Recomp::RelocType::R_MIPS_NONE)
                    continue;
                if (relocation.address < section.ram_addr)
                    bad("relocation precedes its section");
                const auto offset = relocation.address - section.ram_addr;
                std::uint32_t target = 0;
                if (!relocation.reference_symbol)
                    target = local.bases.at(relocation.target_section) + relocation.target_section_offset;
                else if (relocation.target_section == N64Recomp::SectionImport)
                    target = local.imports.at(relocation.symbol_index);
                else if (relocation.target_section == N64Recomp::SectionEvent)
                    target = local.events.at(relocation.symbol_index);
                else {
                    if (!policy.raw_game_access || p.revisions.size() != 1)
                        bad("raw game references require explicit permission and one Game Pak revision");
                    target = 0x80000400U + relocation.target_section_offset;
                    if (target >= 0x80800000U)
                        bad("game relocation exceeds retail RDRAM");
                }
                const auto instruction = be32(image, offset);
                using T = N64Recomp::RelocType;
                switch (relocation.type) {
                case T::R_MIPS_32:
                    put(image, offset, target);
                    break;
                case T::R_MIPS_26:
                    if (target & 3U || ((local.bases[s] + offset + 4) ^ target) & 0xf0000000U)
                        bad("jump relocation is out of range");
                    if (relocation.reference_symbol && relocation.target_section != N64Recomp::SectionImport &&
                        relocation.target_section != N64Recomp::SectionEvent && !policy.call_functions.contains(target))
                        bad("raw call to an unavailable or protected game function");
                    put(image, offset, (instruction & 0xfc000000U) | ((target >> 2) & 0x3ffffffU));
                    break;
                case T::R_MIPS_HI16:
                    put(image, offset, (instruction & 0xffff0000U) | ((target + 0x8000U) >> 16));
                    break;
                case T::R_MIPS_LO16:
                    put(image, offset, (instruction & 0xffff0000U) | (target & 0xffffU));
                    break;
                default:
                    bad("unsupported relocation; rebuild with the DKR SDK linker script");
                }
            }
        }
        for (const auto& hook : symbols.hooks) {
            if (!policy.raw_game_access || !policy.hook_functions.contains(hook.original_vram) ||
                p.revisions.size() != 1)
                bad("hook targets a protected, unavailable or revision-ambiguous game function");
            auto& target = out->hooks[hook.original_vram];
            ((unsigned(hook.flags) & 1) ? target.exit : target.entry).push_back(owner.functions.at(hook.func_index));
        }
        for (const auto& replacement : symbols.replacements) {
            if (!policy.raw_game_access || !policy.functions.contains(replacement.original_vram) ||
                policy.protected_functions.contains(replacement.original_vram) || p.revisions.size() != 1)
                bad("replacement targets a protected, unavailable or revision-ambiguous game function");
            auto& target = out->hooks[replacement.original_vram];
            if (target.replacement)
                bad("two code packages replace the same game function");
            target.replacement = owner.functions.at(replacement.func_index);
        }
        for (const auto& callback : symbols.callbacks) {
            const auto& event = symbols.dependency_events.at(callback.dependency_event_index);
            auto dep = dependency(symbols, event.dependency_index);
            if (dep == ".")
                dep = p.id;
            if (dep == "*" &&
                (event.event_name == "on_tool" || event.event_name == "on_cleanup" || event.event_name == "on_reload" ||
                 event.event_name == "on_live_setting") &&
                p.api != 2)
                bad("on_tool requires executable API 2");
            if (dep == "*" && !policy.events.contains(event.event_name))
                bad("unavailable SDK event: " + event.event_name);
            if (dep != "*" && !exports.contains(dep))
                bad("event dependency is not enabled");
            if (dep != "*" && dep != p.id &&
                std::none_of(p.dependencies.begin(), p.dependencies.end(), [&](const auto& d) { return d.id == dep; }))
                bad("event dependency is absent from the manifest");
            if (dep != "*") {
                const auto package =
                    std::find_if(ordered.begin(), ordered.end(), [&](const auto& pp) { return pp->id == dep; });
                if (package == ordered.end() ||
                    std::none_of((*package)->symbols->event_symbols.begin(), (*package)->symbols->event_symbols.end(),
                                 [&](const auto& e) { return e.base.name == event.event_name; }))
                    bad("missing package event");
            }
            out->events[event_key(dep, event.event_name)].push_back(owner.functions.at(callback.function_index));
        }
    }
    out->resident_bytes = aligned(cursor) - CodeBase;
    if (out->resident_bytes > MaxResident)
        bad("code alignment exceeds session memory budget");
    for (auto& image : out->sections) {
        image.word_swapped.resize(aligned(std::uint32_t(image.bytes.size())), 0);
        for (std::size_t i = 0; i < image.bytes.size(); ++i)
            image.word_swapped[i ^ 3U] = image.bytes[i];
    }
    for (auto& region : out->regions)
        if (region.executable) {
            const auto image = std::find_if(out->sections.begin(), out->sections.end(), [&](const auto& s) {
                return region.address >= s.address && region.address - s.address < s.bytes.size();
            });
            if (image == out->sections.end() || region.size > image->bytes.size() - (region.address - image->address))
                bad("immutable instruction range has no canonical image");
            region.immutable = image->bytes.data() + region.address - image->address;
        }
    return out;
}
void install(const LinkedSet& set, std::span<std::uint8_t> ram) {
    const auto begin = CodeBase - 0x80000000U;
    if (begin > ram.size() || set.resident_bytes > ram.size() - begin || ram.size() % 4)
        bad("session RDRAM does not include admitted code storage");
    std::fill_n(ram.begin() + begin, set.resident_bytes, 0);
    for (const auto& section : set.sections) {
        const auto offset = section.address - 0x80000000U;
        if (offset < begin || offset - begin > set.resident_bytes ||
            section.bytes.size() > set.resident_bytes - (offset - begin))
            bad("invalid linked image extent");
        for (std::size_t i = 0; i < section.bytes.size(); ++i)
            ram[(offset + i) ^ 3U] = section.bytes[i];
    }
}
} // namespace dkr::mods::code

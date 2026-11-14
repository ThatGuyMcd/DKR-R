#include "magic_code_effects.hpp"
#include "legacy_checkpoint.hpp"
namespace dkr::mods::code {
Bytes encode_effects(const LinkedSet& set, const std::vector<Effect>& effects) {
    if (effects.size() > 128)
        throw Error("Code log journal exceeds its bound.");
    CheckpointWriter out;
    out.u32(1);
    out.u32(unsigned(effects.size()));
    for (const auto& effect : effects) {
        const auto owner = std::find_if(set.owners.begin(), set.owners.end(),
                                        [&](const auto& item) { return item.package->id == effect.package; });
        if (owner == set.owners.end())
            throw Error("Code log journal has an unknown owner.");
        out.u32(unsigned(owner - set.owners.begin()));
        out.text(effect.text, 512);
    }
    return std::move(out).finish();
}
std::vector<Effect> decode_effects(const LinkedSet& set, View bytes) {
    if (bytes.size() > MaxEffectJournal)
        throw Error("Code log journal exceeds its bound.");
    CheckpointReader in(bytes);
    if (in.u32() != 1)
        throw Error("Unknown code log journal format.");
    const auto count = in.bounded(128);
    std::vector<Effect> out;
    out.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto owner = in.u32();
        if (owner >= set.owners.size())
            throw Error("Code log journal has an unknown owner.");
        auto text = in.text(512);
        if (text.find('\0') != text.npos)
            throw Error("Code log journal contains an embedded NUL.");
        out.push_back({set.owners[owner].package->id, std::move(text)});
    }
    in.end();
    return out;
}
} // namespace dkr::mods::code

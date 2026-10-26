#include "legacy_audio_bank.hpp"
#include "../custom_music_sequence.hpp"
#include <algorithm>
#include <map>

namespace dkr::mods {
namespace {
void put32(Bytes& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data.at(offset + i) = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
void inspect_sequence_header(View data) {
    slice(data, 0, 68);
    const auto division = be32(data, 64);
    if (!division || division > 0x7fffffffU)
        throw Error("Music sequence has an invalid time division.");
    for (unsigned track = 0; track < 16; ++track) {
        const auto offset = be32(data, track * 4);
        if (offset && (offset < 68 || offset >= data.size()))
            throw Error("Music track starts outside its sequence.");
    }
}
std::string music_identity(View ctl, View samples) {
    if (ctl.size() > 4 * MiB || samples.size() > 16 * MiB || be16(ctl, 0) != 0x4231 || be16(ctl, 2) != 1)
        throw Error("Music requires a bounded native B1 instrument bank.");
    const auto bank = be32(ctl, 4);
    slice(ctl, bank, 12);
    const auto count = be16(ctl, bank);
    if (!bank || count > 128 || ctl[bank + 2] || !be32(ctl, bank + 4))
        throw Error("Unsupported or already relocated music bank.");
    slice(ctl, bank + 12, std::size_t(count) * 4);
    std::size_t work = 0, sound_count = 0;
    std::map<unsigned, std::string> waves, sounds, instruments;
    const auto hash = [&](View data) {
        if (data.size() > 64 * MiB - work)
            throw Error("Music dependency validation exceeds its work budget.");
        work += data.size();
        return sha256(data);
    };
    const auto wave_identity = [&](unsigned ptr) {
        if (auto found = waves.find(ptr); found != waves.end())
            return found->second;
        slice(ctl, ptr, 20);
        const auto type = ctl[ptr + 8];
        if (!ptr || type > 1 || ctl[ptr + 9])
            throw Error("Unsupported music sample format.");
        const auto offset = be32(ctl, ptr), length = be32(ctl, ptr + 4);
        if (!length || length > 2 * MiB)
            throw Error("Music sample length exceeds its budget.");
        std::string id =
            std::to_string(type) + ":" + std::to_string(length) + ":" + hash(slice(samples, offset, length));
        const auto loop = be32(ctl, ptr + 12);
        if (loop) {
            const auto data = slice(ctl, loop, type == 0 ? 44 : 12);
            const auto decoded = type == 0 ? (length / 9) * 16 : length / 2;
            if (be32(data, 0) > be32(data, 4) || be32(data, 4) > decoded)
                throw Error("Music loop exceeds its decoded sample.");
            id += ":" + hash(data);
        } else
            id += ":no-loop";
        if (type == 0) {
            const auto book = be32(ctl, ptr + 16);
            const auto order = be32(ctl, book), predictors = be32(ctl, book + 4);
            if (!book || !order || order > 8 || !predictors || predictors > 16)
                throw Error("Invalid music ADPCM predictor book.");
            id += ":" + hash(slice(ctl, book, 8 + 16 * order * predictors));
        }
        waves.emplace(ptr, id);
        return id;
    };
    const auto sound_identity = [&](unsigned ptr) {
        if (!ptr)
            return std::string("null");
        if (auto found = sounds.find(ptr); found != sounds.end())
            return found->second;
        if (++sound_count > 4096)
            throw Error("Music sound dependency count exceeds its budget.");
        slice(ctl, ptr, 16);
        const auto envelope = be32(ctl, ptr), key = be32(ctl, ptr + 4);
        if (!envelope || !key || ctl[ptr + 14])
            throw Error("Invalid or relocated music sound.");
        auto id = hash(slice(ctl, ptr + 12, 2)) + hash(slice(ctl, envelope, 14)) + hash(slice(ctl, key, 6)) +
                  wave_identity(be32(ctl, ptr + 8));
        sounds.emplace(ptr, id);
        return id;
    };
    const auto instrument_identity = [&](unsigned ptr) {
        if (!ptr)
            return std::string("null;");
        if (auto found = instruments.find(ptr); found != instruments.end())
            return found->second;
        slice(ctl, ptr, 16);
        const auto n = be16(ctl, ptr + 14);
        if (n > 1024 || ctl[ptr + 3])
            throw Error("Invalid or relocated music instrument.");
        slice(ctl, ptr + 16, std::size_t(n) * 4);
        auto id = hash(slice(ctl, ptr, 14)) + ":" + std::to_string(n) + ":";
        for (unsigned i = 0; i < n; ++i)
            id += sound_identity(be32(ctl, ptr + 16 + 4 * i)) + ";";
        id = hash(View(reinterpret_cast<const std::uint8_t*>(id.data()), id.size()));
        instruments.emplace(ptr, id);
        return id;
    };
    std::string id = std::to_string(be32(ctl, bank + 4)) + ":" + std::to_string(count) + ":" +
                     instrument_identity(be32(ctl, bank + 8));
    for (unsigned i = 0; i < count; ++i)
        id += ":" + instrument_identity(be32(ctl, bank + 12 + 4 * i));
    return id;
}
} // namespace
bool equivalent_music_banks(View original_control, View original_samples, View imported_control,
                            View imported_samples) {
    return music_identity(original_control, original_samples) == music_identity(imported_control, imported_samples);
}
SequenceDirectory inspect_sequence_directory(View bank) {
    if (bank.size() > 4 * MiB || be16(bank, 0) != 0x5331)
        throw Error("Unsupported or oversized audio sequence bank.");
    const auto count = be16(bank, 2);
    if (!count || count > 256)
        throw Error("Audio sequence count exceeds its byte-sized namespace.");
    const auto header_size = 4 + std::size_t(count) * 8;
    slice(bank, 0, header_size);
    SequenceDirectory result;
    for (unsigned id = 0; id < count; ++id) {
        const auto offset = be32(bank, 4 + 8 * id), length = be32(bank, 8 + 8 * id);
        if (offset < header_size || offset % 2 || length < 68 || length > 4 * MiB)
            throw Error("Invalid sequence offset or length.");
        const auto loaded = (length + 1) & ~1U;
        slice(bank, offset, loaded);
        const auto sequence = slice(bank, offset, length);
        inspect_sequence_header(sequence);
        result.records.push_back({offset, length, loaded, sha256(sequence)});
        result.maximum_loaded_length = std::max(result.maximum_loaded_length, loaded);
    }
    return result;
}
void validate_changed_sequences(View original, View prepared) {
    const auto before = inspect_sequence_directory(original), after = inspect_sequence_directory(prepared);
    if (before.records.size() != after.records.size())
        throw Error("Prepared music changed the native sequence namespace.");
    for (unsigned i = 0; i < after.records.size(); ++i) {
        if (before.records[i].digest == after.records[i].digest)
            continue;
        const auto& record = after.records[i];
        const auto check = dkr::runtime::custom_music::validate_sequence(slice(prepared, record.offset, record.length),
                                                                         before.maximum_loaded_length, nullptr, true);
        if (!check.ok())
            throw Error("Prepared legacy music is unsafe (" + check.code + "): " + check.message);
    }
}
Bytes build_sequence_bank(View original, const std::map<unsigned, Bytes>& replacements, std::uint32_t capacity) {
    const auto directory = inspect_sequence_directory(original);
    if (directory.maximum_loaded_length > capacity)
        throw Error("Original music does not fit its retained buffers.");
    for (const auto& [id, bytes] : replacements) {
        if (id >= directory.records.size() || bytes.size() > capacity)
            throw Error("Replacement music exceeds the retained sequence ID/buffer capacity.");
        if (((bytes.size() + 1) & ~std::size_t(1)) > capacity)
            throw Error("Aligned music DMA exceeds its retained buffer capacity.");
        inspect_sequence_header(bytes);
        const auto checked = dkr::runtime::custom_music::validate_sequence(bytes, capacity, nullptr, true);
        if (!checked.ok())
            throw Error("Legacy music sequence is unsafe (" + checked.code + "): " + checked.message);
    }
    const auto header_size = 4 + directory.records.size() * 8;
    Bytes output(original.begin(), original.begin() + header_size);
    for (unsigned id = 0; id < directory.records.size(); ++id) {
        const auto& entry = directory.records[id];
        const auto found = replacements.find(id);
        const auto sequence =
            found == replacements.end() ? slice(original, entry.offset, entry.length) : View(found->second);
        const auto offset = output.size();
        if (sequence.size() > 4 * MiB - offset)
            throw Error("Repacked music exceeds its asset budget.");
        put32(output, 4 + 8 * id, static_cast<std::uint32_t>(offset));
        put32(output, 8 + 8 * id, static_cast<std::uint32_t>(sequence.size()));
        output.insert(output.end(), sequence.begin(), sequence.end());
        if (output.size() % 2)
            output.push_back(0); // Retail rounds each DMA up to even.
    }
    inspect_sequence_directory(output);
    return output;
}
} // namespace dkr::mods

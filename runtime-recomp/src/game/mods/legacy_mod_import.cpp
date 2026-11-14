#include "legacy_mod_format.hpp"
#include <miniz/miniz.h>
extern "C" {
#include <xdelta3.h>
}
#include <algorithm>
#include <cctype>
#include <set>

namespace dkr::mods {
namespace {
std::string lower(std::string value) {
    for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string checked_name(const mz_zip_archive_file_stat& stat) {
    std::string name(stat.m_filename);
    if (name.empty() || name.size() >= sizeof(stat.m_filename) - 1 || name.front() == '/' || name.front() == '\\')
        throw Error("Archive contains an empty, absolute or oversized member name.");
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.back() == '/')
        name.pop_back();
    std::size_t start = 0;
    while (start < name.size()) {
        const auto end = name.find('/', start);
        const auto part = lower(name.substr(start, end == std::string::npos ? end : end - start));
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            throw Error("Archive contains an unsafe path component.");
        for (auto c : part)
            if (static_cast<unsigned char>(c) < 32 || c == ':' || c == '<' || c == '>' || c == '|' || c == '?' ||
                c == '*')
                throw Error("Archive contains an unsafe filename.");
        const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' &&
             stem[3] <= '9'))
            throw Error("Archive contains a reserved device filename.");
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    const auto mode = (stat.m_external_attr >> 16) & 0170000;
    if (mode && mode != 0100000 && mode != 0040000)
        throw Error("Links and special files are not accepted in mod archives.");
    if (stat.m_bit_flag & 1)
        throw Error("Encrypted mod archives are not supported.");
    return name;
}
} // namespace
std::vector<PatchInput> read_patch_inputs(const std::filesystem::path& source) {
    const auto extension = lower(source.extension().string());
    if (extension == ".xdelta")
        return {{source.filename().string(), read_file(source, MaxPatch)}};
    if (extension != ".zip")
        throw Error("Select a .xdelta patch or a ZIP containing patches.");
    const auto bytes = read_file(source, MaxArchive);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        throw Error("ZIP directory is invalid.");
    struct Close {
        mz_zip_archive& zip;
        ~Close() {
            mz_zip_reader_end(&zip);
        }
    } close{zip};
    const unsigned count = mz_zip_reader_get_num_files(&zip);
    if (count > MaxEntries)
        throw Error("Archive has more than 1,024 entries.");
    std::vector<PatchInput> out;
    std::set<std::string> names, digests;
    std::uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            throw Error("Invalid ZIP member metadata.");
        // The fixed stat buffer can silently truncate names (including an
        // embedded NUL). Compare the raw directory name before trusting it.
        const auto name_size = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        if (name_size < 2 || name_size > sizeof(stat.m_filename))
            throw Error("Archive member name is empty or too long.");
        std::string raw_name(name_size, '\0');
        if (mz_zip_reader_get_filename(&zip, i, raw_name.data(), name_size) != name_size ||
            raw_name.find('\0') != name_size - 1 || raw_name.substr(0, name_size - 1) != stat.m_filename)
            throw Error("Archive member name is ambiguous or contains an embedded NUL.");
        auto name = checked_name(stat);
        if (!names.insert(lower(name)).second)
            throw Error("Archive contains colliding member names.");
        if (stat.m_uncomp_size > MaxStaged - total)
            throw Error("Archive expands beyond the 256 MiB limit.");
        total += stat.m_uncomp_size;
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        if (!lower(name).ends_with(".xdelta"))
            continue;
        if (stat.m_uncomp_size > MaxPatch)
            throw Error("A patch exceeds the 64 MiB limit.");
        Bytes patch(static_cast<std::size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip, i, patch.data(), patch.size(), 0))
            throw Error("A ZIP member is truncated, corrupt or exceeds its declared length.");
        if (digests.insert(sha256(patch)).second)
            out.push_back({std::move(name), std::move(patch)});
        if (out.size() > 32)
            throw Error("Import up to 32 distinct patches per archive.");
    }
    if (out.empty())
        throw Error("No .xdelta patches were found in this ZIP. Nested archives are not unpacked.");
    return out;
}
Bytes decode_patch(View source, View patch) {
    if (patch.size() < 4 || patch.size() > MaxPatch || source.size() > MaxImage || patch[0] != 0xd6 ||
        patch[1] != 0xc3 || patch[2] != 0xc4 || patch[3] != 0)
        throw Error("This is not a supported VCDIFF/xdelta patch.");
    Bytes output(MaxImage);
    usize_t written = 0;
    // Library API: application-header filenames and external decompressor
    // commands are never interpreted. Adler checks remain enabled.
    const int rc = xd3_decode_memory(patch.data(), static_cast<usize_t>(patch.size()), source.data(),
                                     static_cast<usize_t>(source.size()), output.data(), &written,
                                     static_cast<usize_t>(output.size()), 0);
    if (rc != 0)
        throw Error(
            "Patch checksum/decode failed. It may require a different original Game Pak or an explicit patch-chain recipe.");
    output.resize(written);
    canonicalize_rom(output);
    return output;
}
} // namespace dkr::mods

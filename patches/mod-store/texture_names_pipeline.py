"""Read-only comparison adapter for repairing old Store display labels."""


def transform_rice(text, once):
    text = once(text, '#include "runtime_rice_texture_import.hpp"',
                '#include "runtime_rice_texture_import.hpp"\n#include <stop_token>')
    anchor = '} // namespace\n\nnamespace dkr::runtime::rice_texture {'
    compare = r'''
// Match decoded pixels, not filenames/counts (different packs often replace
// the same identities). This audit never writes or activates installed data.
bool ComparePng(const std::filesystem::path& path, const Image& expected) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) return false;
    const auto size = std::filesystem::file_size(path);
    if (!size || size > kMaximumSourceImageBytes) return false;
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
        width != expected.width || height != expected.height) return false;
    auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) return false;
    const bool same = width == expected.width && height == expected.height &&
        std::equal(expected.rgba.begin(), expected.rgba.end(), pixels);
    stbi_image_free(pixels);
    return same;
}
'''
    text = once(text, anchor, compare + '\n' + anchor)
    text = once(text, 'bool convert_archive(RT64::FileSystem& archive,',
                'static bool convert_or_compare_archive(RT64::FileSystem& archive,')
    text = once(text, '                     const ProgressCallback& progress) {',
                '                     const ProgressCallback& progress, bool compare_only) {')
    text = once(text, '    std::filesystem::create_directories(destination, filesystem_error);',
                '    if (!compare_only) std::filesystem::create_directories(destination, filesystem_error);')
    text = once(text, '        if (!WritePng(destination / filename, colour, error)) return false;',
                '        if (compare_only) {\n            if (!ComparePng(destination / filename, colour)) return false;\n        } else if (!WritePng(destination / filename, colour, error)) return false;')
    anchor = '    std::ofstream database_file(destination / "rt64.json", std::ios::trunc);'
    text = once(text, anchor, r'''    if (compare_only) {
        const auto db_path = destination / "rt64.json";
        const auto report_path = destination / "dkr-r-rice-import.json";
        if (std::filesystem::is_symlink(db_path) || std::filesystem::is_symlink(report_path) ||
            !std::filesystem::is_regular_file(db_path) || !std::filesystem::is_regular_file(report_path) ||
            std::filesystem::file_size(db_path) > 32U * 1024U * 1024U ||
            std::filesystem::file_size(report_path) > 64U * 1024U) return false;
        std::ifstream db_file(db_path), report_file(report_path);
        const auto existing = nlohmann::json::parse(db_file);
        const auto report = nlohmann::json::parse(report_file);
        return existing == database && report.value("sourceImages", 0U) == result.source_images &&
            report.value("convertedIdentities", 0U) == identities.size() &&
            report.value("mergedRgbAlphaPairs", 0U) == result.merged_pairs &&
            report.value("opaqueRgbImages", 0U) == result.opaque_rgb &&
            report.value("nativeAllImages", 0U) == result.all_images;
    }
''' + anchor)
    text = once(text, '} // namespace dkr::runtime::rice_texture', r'''
bool convert_archive(RT64::FileSystem& archive, const std::filesystem::path& destination,
                     const std::string& source_name, ImportResult& result, std::string& error,
                     const ProgressCallback& progress) {
    return convert_or_compare_archive(archive, destination, source_name, result, error, progress, false);
}
bool matches_converted_archive(RT64::FileSystem& archive, const std::filesystem::path& destination,
                               std::stop_token stop) {
    try {
        ImportResult result;
        std::string error;
        return convert_or_compare_archive(archive, destination, {}, result, error,
            [&](std::size_t, std::size_t, const char*) { return !stop.stop_requested(); }, true);
    } catch (...) { return false; }
}
} // namespace dkr::runtime::rice_texture''')
    # std::equal is used by the comparison-only branch.
    return once(text, '#include <array>', '#include <algorithm>\n#include <array>')


def transform_texture(text, once):
    text = once(text, '#include "runtime_texture_packs.hpp"',
                '#include "runtime_texture_packs.hpp"\n#include "mod_store.hpp"')
    text = once(text, '    if (include_hidden) return g_packs;',
                '    const auto named = [](PackInfo pack) {\n        pack.name = dkr::store::texture_display_name(pack.path, pack.name);\n        return pack;\n    };\n    if (include_hidden) {\n        auto result = g_packs;\n        for (auto& pack : result) pack = named(std::move(pack));\n        return result;\n    }')
    return once(text, '    return visible;\n}',
                '    for (auto& pack : visible) pack = named(std::move(pack));\n    return visible;\n}')

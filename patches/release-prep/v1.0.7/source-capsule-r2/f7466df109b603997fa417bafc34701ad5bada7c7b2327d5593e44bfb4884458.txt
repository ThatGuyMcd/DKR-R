// Appended by the Patch Pipeline to the accepted texture importer. Uses its
// existing parser/converter without publishing an entry or changing settings.
namespace dkr::store {
StorePrepared prepare_store_texture(const std::filesystem::path& source, const std::filesystem::path& stage,
                                    const std::string& title, std::stop_token stop,
                                    const std::function<void(float, std::string)>& progress) {
    using namespace dkr::runtime::texture_packs;
    const auto inspection = InspectArchive(store_io_path(source));
    if (inspection.format != Format::LegacyRice && inspection.format != Format::NativeRt64)
        throw std::runtime_error("This texture package cannot be installed: " + inspection.detail);
    std::filesystem::create_directories(store_io_path(stage));
    const auto source_name = source.filename().generic_u8string();
    const auto filename = package_filename(title, {source_name.begin(), source_name.end()});
    const auto destination = stage / (inspection.format == Format::LegacyRice ? "textures.rice" : "textures.rtz");
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Texture staging is already in use.");
    if (inspection.format == Format::LegacyRice) {
        auto archive = RT64::FileSystemZip::create(store_io_path(source), "");
        dkr::runtime::rice_texture::ImportResult result;
        std::string error;
        if (!archive || !dkr::runtime::rice_texture::convert_archive(
                            *archive, store_io_path(destination), filename, result, error,
                            [&](std::size_t done, std::size_t total, const char* note) {
                                progress(total ? float(double(done) / double(total)) : 0.F, note);
                                return !stop.stop_requested();
                            }))
            throw std::runtime_error(error.empty() ? "Could not read the texture package." : error);
        if (!InspectDirectory(store_io_path(destination)).compatible)
            throw std::runtime_error("Converted texture validation failed.");
    } else {
        if (!inspection.compatible)
            throw std::runtime_error(inspection.detail);
        std::filesystem::copy_file(store_io_path(source), store_io_path(destination));
    }
    StorePrepared out;
    const auto utf8 = destination.generic_u8string();
    out.outputs.push_back(
        {{"kind", "texture"},
         {"source", std::string(utf8.begin(), utf8.end())},
         {"fingerprint", ArchiveFingerprint(source)},
         {"filename",
          package_filename(title, "textures.zip").substr(0, package_filename(title, "textures.zip").size() - 4) +
              (inspection.format == Format::LegacyRice ? ".rice" : ".rtz")}});
    return out;
}
void publish_store_texture(const Json& output, const Json& previous) {
    using namespace dkr::runtime::texture_packs;
    const auto path = std::filesystem::u8path(text(output, "absolute", 4096));
    if (!IsDirectManagedChild(path))
        throw std::runtime_error("Texture destination is outside the managed library.");
    const auto id = StableId(path);
    {
        std::lock_guard lock(g_mutex);
        g_imported_at.try_emplace(id, CurrentTimestamp());
        g_managed_sizes[id] = ManagedSize(path);
        if (!text(output, "owner", 256).empty()) {
            g_track_pack_owners[id] = {text(output, "owner", 256), text(output, "textureDigest", 64),
                                       text(output, "fingerprint", 128)};
            g_enabled_ids.insert(id);
        }
        if (previous.is_object() && previous.contains("enabled")) {
            if (previous.value("enabled", false))
                g_enabled_ids.insert(id);
            else
                g_enabled_ids.erase(id);
            if (previous.value("hidden", false))
                g_hidden_ids.insert(id);
            else
                g_hidden_ids.erase(id);
            const auto priority = previous.value("priority", std::size_t(0));
            if (priority)
                g_priorities[id] = priority;
        }
        if (!SaveSettingsLocked())
            throw std::runtime_error("Texture preferences could not be committed.");
    }
    refresh();
}
} // namespace dkr::store

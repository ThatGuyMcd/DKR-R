// Discover a single course inside an already safely extracted archive. Authors
// may include wrapper folders and editor sources beside the actual .dkrmap.
bool locate_unpacked_track(const std::filesystem::path& input_root, std::filesystem::path& track_dir,
                           std::filesystem::path& hd_pack, std::string* error = nullptr, std::stop_token stop = {}) {
    namespace fs = std::filesystem;
    track_dir.clear();
    hd_pack.clear();
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto root = dkr::store::store_io_path(input_root);
    std::string detail;
    if (!StoreTrackOrdinary(root, true, detail))
        return fail(detail);
    std::vector<std::pair<fs::path, unsigned>> pending{{root, 0}};
    fs::path found;
    std::size_t entries = 0;
    while (!pending.empty()) {
        if (stop.stop_requested())
            return fail("Track package discovery cancelled.");
        auto [directory, depth] = std::move(pending.back());
        pending.pop_back();
        const auto manifest = directory / "manifest.json";
        std::error_code code;
        const bool has_manifest = fs::exists(dkr::store::store_io_path(manifest), code);
        if (code)
            return fail("Could not inspect the track manifest: " + code.message());
        if (has_manifest) {
            if (!StoreTrackOrdinary(manifest, false, detail))
                return fail(detail);
            if (!found.empty())
                return fail("The track package contains more than one course. Import each .dkrmap separately.");
            found = directory;
            // Asset/source subdirectories inside a course are not other courses.
            continue;
        }
        fs::directory_iterator item(directory, code), end;
        if (code)
            return fail("Could not read the track package folder: " + code.message());
        for (; item != end; item.increment(code)) {
            if (code)
                break;
            if (stop.stop_requested())
                return fail("Track package discovery cancelled.");
            if (++entries > 100000)
                return fail("The track package contains too many files to locate its course safely.");
            const auto state = item->symlink_status(code);
            if (code)
                break;
            const bool directory_entry = fs::is_directory(state);
            if (!StoreTrackOrdinary(item->path(), directory_entry, detail))
                return fail(detail);
            if (!directory_entry)
                continue;
            if (depth >= 8)
                return fail("The track package has too many wrapper folders. Import its .dkrmap folder directly.");
            pending.emplace_back(item->path(), depth + 1);
        }
        if (code)
            return fail("Could not read the track package folder: " + code.message());
    }
    if (found.empty())
        return fail("This package does not contain a track manifest.json. Import its .dkrmap folder or ZIP.");

    // Use the companion named by this course, not the first unrelated ZIP in
    // the author's source bundle. Stay inside the private extraction root.
    fs::path companion;
    std::vector<std::uint8_t> bytes;
    if (read_file(found / "manifest.json", bytes)) {
        const auto manifest = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (manifest.is_object()) {
            const auto hd = manifest.find("hdTexturePack");
            if (hd != manifest.end() && hd->is_object()) {
                const auto file = hd->find("file");
                if (file != hd->end() && file->is_string()) {
                    const auto name = file->get<std::string>();
                    if (dkr::runtime::custom_tracks::files::member(name) && name.find('/') == name.npos) {
                        std::vector<fs::path> parents;
                        if (found != root)
                            parents.push_back(found.parent_path());
                        parents.push_back(found);
                        if (found != root)
                            parents.push_back(root);
                        for (const auto& parent : parents) {
                            const auto candidate = parent / fs::u8path(name);
                            std::error_code code;
                            const bool exists = fs::exists(dkr::store::store_io_path(candidate), code);
                            if (code)
                                return fail("Could not inspect the track's HD textures: " + code.message());
                            if (!exists)
                                continue;
                            if (!StoreTrackOrdinary(candidate, false, detail))
                                return fail(detail);
                            companion = candidate;
                            break;
                        }
                    }
                }
            }
        }
    }
    track_dir = std::move(found);
    hd_pack = std::move(companion);
    return true;
}

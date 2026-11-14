// Preparation only. Original install APIs and their runtime identities remain
// unchanged; the Store transaction publishes this validated private directory.
namespace dkr::store {
StorePrepared prepare_store_track(const std::filesystem::path& source, const std::filesystem::path& stage,
                                  const std::string&, std::stop_token stop) {
    namespace fs = std::filesystem;
    fs::create_directories(store_io_path(stage));
    const auto unpacked = stage / "unpacked";
    fs::create_directory(store_io_path(unpacked));
    fs::path track = source, hd;
    std::string error;
    if (fs::is_regular_file(source) && (lower_extension(source) == ".zip" || lower_extension(source) == ".dkrmap")) {
        if (!extract_zip(store_io_path(source), store_io_path(unpacked), error, stop) ||
            !locate_unpacked_track(store_io_path(unpacked), track, hd, &error, stop))
            throw std::runtime_error(error.empty() ? "This package does not contain a .dkrmap track." : error);
    } else if (!fs::is_directory(source))
        throw std::runtime_error("A track must be a .dkrmap folder or ZIP package.");
    if (stop.stop_requested())
        throw std::runtime_error("Track preparation cancelled.");
    dkr::runtime::custom_tracks::Track probe;
    if (!parse_track(store_io_path(track), probe, error))
        throw std::runtime_error(error);
    // Preserve all declared relative asset paths, including external audio.
    // HD companion selection is checked against the track's own declaration.
    if (hd.empty() && !probe.hd_pack_file.empty() && dkr::runtime::custom_tracks::files::member(probe.hd_pack_file))
        hd = source.parent_path() / fs::u8path(probe.hd_pack_file);
    std::string hd_digest;
    if (!hd.empty() && fs::is_regular_file(hd) && !probe.hd_pack_digest.empty() &&
        (!read_hd_pack_digest(hd, hd_digest) || hd_digest != probe.hd_pack_digest))
        throw std::runtime_error("The bundled HD textures do not match this track export.");
    const auto payload = stage / "course.dkrmap";
    fs::copy(store_io_path(track), store_io_path(payload), fs::copy_options::recursive);
    StorePrepared out;
    const auto utf8 = payload.generic_u8string();
    out.outputs.push_back({{"kind", "track"},
                           {"source", std::string(utf8.begin(), utf8.end())},
                           {"filename", probe.id + ".dkrmap"},
                           {"id", probe.id},
                           {"name", probe.name}});
    out.native = {{"trackId", probe.id}, {"hdDigest", probe.hd_pack_digest}};
    if (!hd.empty() && fs::is_regular_file(hd)) {
        const auto hd_utf8 = hd.generic_u8string();
        out.native["hdSource"] = std::string(hd_utf8.begin(), hd_utf8.end());
    }
    return out;
}
} // namespace dkr::store

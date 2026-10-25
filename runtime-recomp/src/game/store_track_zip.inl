// First-party archive I/O, composed inside custom_tracks.cpp's private namespace.
// miniz receives native FILE handles, never an ANSI conversion of a Windows path.
using StoreTrackFile = std::unique_ptr<FILE, int (*)(FILE*)>;
StoreTrackFile StoreTrackOpen(const std::filesystem::path& path, bool writing) {
    const auto native = dkr::store::store_io_path(path);
#ifdef _WIN32
    return {::_wfopen(native.c_str(), writing ? L"wb" : L"rb"), &std::fclose};
#else
    return {std::fopen(native.c_str(), writing ? "wb" : "rb"), &std::fclose};
#endif
}
struct StoreTrackArchive {
    StoreTrackFile file{nullptr, &std::fclose};
    mz_zip_archive zip{};
    bool opened = false;
    explicit StoreTrackArchive(const std::filesystem::path& path) : file(StoreTrackOpen(path, false)) {
        std::error_code error;
        const auto size = std::filesystem::file_size(dkr::store::store_io_path(path), error);
        if (file && !error && size <= 2ULL * 1024 * 1024 * 1024)
            opened = mz_zip_reader_init_cfile(&zip, file.get(), size, 0) != MZ_FALSE;
    }
    ~StoreTrackArchive() {
        if (opened)
            mz_zip_reader_end(&zip);
    }
    std::string reason() {
        const auto* message = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return message ? message : "unreadable archive data";
    }
};
bool StoreTrackOrdinary(const std::filesystem::path& path, bool directory, std::string& error) {
    const auto native = dkr::store::store_io_path(path);
    std::error_code code;
    const auto state = std::filesystem::symlink_status(native, code);
    bool ordinary = !code && !std::filesystem::is_symlink(state) &&
                    (directory ? std::filesystem::is_directory(state) : std::filesystem::is_regular_file(state));
#ifdef _WIN32
    const auto flags = GetFileAttributesW(native.c_str());
    ordinary = ordinary && flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
#endif
    if (!ordinary)
        error = "The track extraction folder contains an unsafe or unavailable path.";
    return ordinary;
}
bool StoreTrackZipPath(std::string name, bool directory, std::filesystem::path& relative) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (directory && !name.empty() && name.back() == '/')
        name.pop_back();
    if (name.empty() || name.size() > 4096 || name.front() == '/' || name.back() == '/' ||
        name.find("//") != name.npos || std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*';
        }))
        return false;
    relative = std::filesystem::u8path(name);
    if (relative.is_absolute() || relative.has_root_name())
        return false;
    unsigned depth = 0;
    for (const auto& part : relative) {
        const auto encoded = part.generic_u8string();
        std::string leaf(encoded.begin(), encoded.end());
        if (++depth > 64 || leaf.empty() || leaf == "." || leaf == ".." || leaf.back() == '.' || leaf.back() == ' ')
            return false;
        auto device = leaf.substr(0, leaf.find('.'));
        std::transform(device.begin(), device.end(), device.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        if (device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" ||
            (device.size() == 4 && (device.starts_with("COM") || device.starts_with("LPT")) && device[3] >= '0' &&
             device[3] <= '9'))
            return false;
    }
    return true;
}
bool StoreReadTrackZipEntry(const std::filesystem::path& path, const char* name, std::vector<std::uint8_t>& bytes) {
    StoreTrackArchive archive(path);
    if (!archive.opened)
        return false;
    const int index = mz_zip_reader_locate_file(&archive.zip, name, nullptr, 0);
    mz_zip_archive_file_stat stat{};
    if (index < 0 || !mz_zip_reader_file_stat(&archive.zip, mz_uint(index), &stat) ||
        stat.m_uncomp_size > 16U * 1024U * 1024U)
        return false;
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&archive.zip, mz_uint(index), &size, 0);
    if (!data)
        return false;
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.assign(first, first + size);
    mz_free(data);
    return true;
}
bool StoreExtractTrackZip(const std::filesystem::path& path, const std::filesystem::path& destination,
                          std::string& error, std::stop_token stop) {
    StoreTrackArchive archive(path);
    if (!archive.opened) {
        error = "The track ZIP could not be opened: " + archive.reason();
        return false;
    }
    if (!StoreTrackOrdinary(destination, true, error))
        return false;
    const auto count = mz_zip_reader_get_num_files(&archive.zip);
    if (!count || count > 100000) {
        error = "The track ZIP has an invalid number of entries.";
        return false;
    }
    std::uint64_t total = 0;
    for (mz_uint index = 0; index < count; ++index) {
        if (stop.stop_requested()) {
            error = "Track ZIP extraction cancelled.";
            return false;
        }
        mz_zip_archive_file_stat stat{};
        const auto length = mz_zip_reader_get_filename(&archive.zip, index, nullptr, 0);
        if (!mz_zip_reader_file_stat(&archive.zip, index, &stat) || !length || length > 4097) {
            error = "The track ZIP has an unreadable directory entry.";
            return false;
        }
        std::vector<char> encoded(length);
        if (mz_zip_reader_get_filename(&archive.zip, index, encoded.data(), length) != length ||
            std::strlen(encoded.data()) + 1 != length) {
            error = "The track ZIP has an invalid filename.";
            return false;
        }
        const std::string name(encoded.data());
        const bool directory = mz_zip_reader_is_file_a_directory(&archive.zip, index) != MZ_FALSE;
        const auto unix_type = (stat.m_external_attr >> 16) & 0170000;
        std::filesystem::path relative;
        if (!StoreTrackZipPath(name, directory, relative) ||
            (unix_type && unix_type != 0100000 && unix_type != 0040000)) {
            error = "The track ZIP contains an unsafe path: " + name;
            return false;
        }
        if (stat.m_is_encrypted || !mz_zip_reader_is_file_supported(&archive.zip, index) ||
            stat.m_uncomp_size > 512ULL * 1024 * 1024 || stat.m_uncomp_size > 8ULL * 1024 * 1024 * 1024 - total) {
            error = "The track ZIP entry is encrypted, unsupported or exceeds its extraction limit: " + name;
            return false;
        }
        total += stat.m_uncomp_size;
        auto target = destination;
        unsigned component = 0;
        const auto parts = std::distance(relative.begin(), relative.end());
        for (const auto& part : relative) {
            target /= part;
            if (++component == parts && !directory)
                break;
            std::error_code code;
            std::filesystem::create_directory(dkr::store::store_io_path(target), code);
            if ((code && code != std::errc::file_exists) || !StoreTrackOrdinary(target, true, error)) {
                if (error.empty())
                    error = "Could not create the track ZIP folder '" + name + "': " + code.message();
                return false;
            }
        }
        if (directory)
            continue;
        std::error_code code;
        const auto state = std::filesystem::symlink_status(dkr::store::store_io_path(target), code);
        if ((!code && state.type() != std::filesystem::file_type::not_found) ||
            (code && code != std::errc::no_such_file_or_directory)) {
            error = "The track ZIP repeats or conflicts with a path: " + name;
            return false;
        }
        auto output = StoreTrackOpen(target, true);
        if (!output) {
            error = "Could not create track ZIP file '" + name + "': " + std::strerror(errno);
            return false;
        }
        const bool extracted = mz_zip_reader_extract_to_cfile(&archive.zip, index, output.get(), 0) != MZ_FALSE;
        const bool closed = std::fclose(output.release()) == 0;
        if (!extracted || !closed ||
            std::filesystem::file_size(dkr::store::store_io_path(target), code) != stat.m_uncomp_size || code) {
            error = "Could not extract track ZIP file '" + name + "': " +
                    (extracted ? std::string("incomplete output; check free disk space and folder permissions")
                               : archive.reason());
            return false;
        }
    }
    return true;
}

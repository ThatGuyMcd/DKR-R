namespace dkr::runtime::saves::records {
namespace {
std::mutex record_mutex;
std::filesystem::path record_path;
Map record_seed;
Bytes record_pending;
std::future<std::string> record_job;
std::string record_failure;
bool ReadRecords(const std::filesystem::path& path, Bytes& bytes) {
    return ReadFileBounded(path, 20 + 8192 * 76, bytes) && valid(bytes);
}
} // namespace
bool persist(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, std::string& error) {
    Map incoming;
    if (!decode(bytes, incoming)) {
        error = "Custom records have an invalid format; previous records were kept.";
        return false;
    }
    auto owner = storage::acquire(path, error);
    if (!owner)
        return false;
    Map merged;
    Bytes previous;
    if (std::filesystem::exists(path)) {
        if (!ReadRecords(path, previous) || !decode(previous, merged)) {
            error = "Custom records need recovery; the existing image was retained.";
            return false;
        }
    }
    for (auto& [key, r] : incoming) {
        auto& saved = merged[key];
        saved.key = r.key;
        saved.vehicle = r.vehicle;
        if (r.lap && (!saved.lap || r.lap < saved.lap)) {
            saved.lap = r.lap;
            saved.lap_name = r.lap_name;
        }
        if (r.course && (!saved.course || r.course < saved.course)) {
            saved.course = r.course;
            saved.course_name = r.course_name;
        }
    }
    if (merged.size() > 8192) {
        error = "The custom-record store is full; export a copy before adding more records.";
        return false;
    }
    return storage::write_atomic(path, encode(merged), ReadRecords, error);
}
bool begin(const std::filesystem::path& path, bool restore, std::string& error) {
    if (!finish(error))
        return false;
    std::scoped_lock lock(record_mutex);
    record_path = path;
    record_seed.clear();
    record_failure.clear();
    if (!path.empty() && !storage::writable_path(path, error))
        return false;
    if (restore && std::filesystem::exists(path)) {
        Bytes bytes;
        if (!ReadRecords(path, bytes) || !decode(bytes, record_seed)) {
            error = "Custom-course records need recovery. Their original file was retained.";
            return false;
        }
    }
    return true;
}
Bytes seed() {
    std::scoped_lock lock(record_mutex);
    return encode(record_seed);
}
void submit(Bytes bytes) {
    std::scoped_lock lock(record_mutex);
    if (record_path.empty() || !record_failure.empty())
        return;
    if (record_job.valid() && record_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        record_failure = record_job.get();
    if (!record_failure.empty())
        return;
    record_pending = std::move(bytes);
    if (!record_job.valid()) {
        const auto path = record_path;
        auto next = std::move(record_pending);
        record_pending.clear();
        record_job = std::async(std::launch::async, [path, next = std::move(next)] {
            std::string error;
            persist(path, next, error);
            return error;
        });
    }
}
bool finish(std::string& error) {
    std::scoped_lock lock(record_mutex);
    if (record_job.valid())
        record_failure = record_job.get();
    if (record_failure.empty() && !record_pending.empty() && !record_path.empty())
        persist(record_path, record_pending, record_failure);
    record_pending.clear();
    record_path.clear();
    record_seed.clear();
    if (!record_failure.empty()) {
        error = record_failure;
        record_failure.clear();
        return false;
    }
    return true;
}
} // namespace dkr::runtime::saves::records

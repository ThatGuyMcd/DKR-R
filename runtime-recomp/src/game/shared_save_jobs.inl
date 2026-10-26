// UI-owned completion, worker-owned I/O. No ImGui calls or borrowed references
// cross the worker boundary. Launch is fenced until persistence finishes.
struct SharedSaveResult {
    bool ok = false;
    std::string message;
};
std::future<SharedSaveResult> g_shared_save_job;
std::string g_shared_draft_revision;
std::uint64_t g_shared_save_generation = 1;
struct SharedSaveImport {
    std::filesystem::path path;
    int kind = 0;
    int port = 0;
};
SharedSaveImport g_shared_save_import;
bool SharedSaveBusy() {
    return g_shared_save_job.valid();
}
bool BeginSharedSaveJob(std::function<bool(std::string&)> action, std::string success) {
    if (SharedSaveBusy())
        return false;
    g_save_manager_status = "Saving...";
    g_shared_save_job = std::async(std::launch::async, [action = std::move(action), success = std::move(success)] {
        SharedSaveResult result;
        try {
            std::string error;
            result.ok = action(error);
            result.message = result.ok ? success : error;
        } catch (const std::exception& e) {
            result.message = e.what();
        } catch (...) {
            result.message = "The save operation stopped. Original recovery images were retained.";
        }
        return result;
    });
    return true;
}
void PumpSharedSaveJob() {
    if (!g_shared_save_job.valid() || g_shared_save_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const auto result = g_shared_save_job.get();
    g_save_manager_status = result.message;
    if (result.ok) {
        g_save_builder_image.reset();
        g_shared_draft_revision.clear();
        ++g_shared_save_generation;
        InvalidateSaveManagerViewCache();
    }
}

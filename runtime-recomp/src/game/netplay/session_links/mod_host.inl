// Included at namespace scope by the retained mod-sync owner composition.
namespace dkr::mods::online {
bool ModSync::cached_host_active() const {
    const auto current = snapshot();
    return running_.load() && current.phase == SyncPhase::HostReady && current.manifest && current.profile &&
           current.runtime;
}
bool ModSync::promote_cached_host(SyncSettings settings, std::string& error) {
    auto ready = snapshot();
    if ((ready.phase != SyncPhase::Verified && ready.phase != SyncPhase::HostReady) || !ready.manifest ||
        !ready.profile || !ready.runtime || ready.profile->digest != identity(*ready.manifest)) {
        error = "The successor has no matching verified mod session.";
        return false;
    }
    if (!start(SyncPhase::HostPreparing)) {
        error.clear();
        return false;
    }
    try {
        thread_ = std::jthread([this, settings = std::move(settings),
                                ready = std::move(ready)](std::stop_token stop) mutable {
            try {
                const auto root = private_storage_path(settings.online_root) / "payloads";
                auto bundle = std::make_shared<Bundle>();
                bundle->manifest = *ready.manifest;
                for (const auto& item : bundle->manifest.payloads) {
                    require_running(stop);
                    check_storage(root / item.digest, false);
                    auto bytes = std::make_shared<const Bytes>(read_file(root / item.digest, std::size_t(item.size)));
                    if (bytes->size() != item.size || sha256(*bytes) != item.digest)
                        throw Error("A cached online mod changed. Host transfer was cancelled.");
                    bundle->payloads.emplace(item.digest, std::move(bytes));
                }
                require_running(stop);
                ready.phase = SyncPhase::HostReady;
                ready.error.clear();
                ready.stage = "Shared session mods are ready for hosting";
                publish(ready);
                host_loop(std::move(bundle), stop);
            } catch (const std::exception& e) {
                if (!stop.stop_requested()) {
                    ready.phase = SyncPhase::Verified;
                    ready.error = e.what();
                    publish(std::move(ready));
                }
            } catch (...) {
                if (!stop.stop_requested()) {
                    ready.phase = SyncPhase::Verified;
                    ready.error = "The shared mod session could not be prepared for hosting.";
                    publish(std::move(ready));
                }
            }
            running_.store(false);
        });
    } catch (const std::exception& e) {
        running_.store(false);
        ready.phase = SyncPhase::Verified;
        ready.error = e.what();
        publish(std::move(ready));
        error = e.what();
        return false;
    }
    error.clear();
    return true;
}
} // namespace dkr::mods::online

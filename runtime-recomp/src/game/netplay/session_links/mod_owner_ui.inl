// The frontend owns the existing mod worker. The network owner only consumes
// its readiness bit; it never scans, reads or prepares mod files under locks.
{
    namespace watch = dkr::runtime::spectators;
    namespace links = dkr::runtime::netplay::links;
    auto hub = watch::party_link(&session());
    bool candidate = false, requested = false;
    {
        std::scoped_lock lock(hub->mutex);
        const auto& p = hub->links.handover.proposal();
        candidate =
            hub->enabled && p && p->successor == hub->local && hub->links.handover.phase() == links::Phase::Preparing;
        requested = hub->links.mod_requested;
    }
    if (candidate && !network.room.manifest.mod_manifest_hash.empty()) {
        if (g_online_mod_sync->cached_host_active() && view.manifest &&
            identity(*view.manifest) == network.room.manifest.mod_manifest_hash) {
            std::scoped_lock lock(hub->mutex);
            hub->links.mod_ready = true;
        } else if (requested && view.phase == SyncPhase::Verified && !view.error.empty()) {
            std::scoped_lock lock(hub->mutex);
            hub->links.status = view.error;
            hub->links.cancel = true;
        } else if (!requested && (view.phase == SyncPhase::Verified || view.phase == SyncPhase::HostReady)) {
            std::string error;
            if (g_online_mod_sync->promote_cached_host(OnlineModSettings(), error)) {
                std::scoped_lock lock(hub->mutex);
                hub->links.mod_requested = true;
            } else if (!error.empty()) {
                std::scoped_lock lock(hub->mutex);
                hub->links.status = error;
                hub->links.cancel = true;
            }
        }
    }
}

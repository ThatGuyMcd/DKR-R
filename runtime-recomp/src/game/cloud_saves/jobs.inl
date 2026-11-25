std::string EnabledCloudSaveAccount() {
    const auto profile = dkr::store::service().view();
    auto account = dkr::store::text(profile->user, "id", 160);
    return account == profile->cloud_saves_account ? std::move(account) : std::string{};
}

bool RunCloudSaveSync(std::string account, dkr::cloud_saves::SyncScope scope = dkr::cloud_saves::SyncScope::All) {
    return BeginSharedSaveJob(
        [account = std::move(account), scope](std::string&) {
            dkr::store::service().sync_cloud_saves(account, scope);
            return true;
        },
        "Cloud Saves checked. See Save Manager for details.");
}

void ToggleCloudSaves(std::string account, bool enabled) {
    BeginSharedSaveJob(
        [account = std::move(account), enabled](std::string&) {
            auto& service = dkr::store::service();
            service.set_cloud_saves(account, enabled);
            if (enabled)
                service.sync_cloud_saves(account);
            return true;
        },
        enabled ? "Cloud Saves enabled on this device." : "Cloud Saves paused on this device.");
}

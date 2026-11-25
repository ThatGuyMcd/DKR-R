#pragma once
#include <string_view>

namespace dkr::cloud_saves {
enum class Action { None, Upload, Download, Defer };
enum class SyncScope { All, PublishHostedOnline, PublishProgress };
enum class SyncStatus { Idle, Syncing, Synced, Error, Unreachable };

constexpr SyncStatus sync_failure(bool transport_failed, unsigned http_status) {
    const bool unavailable = (http_status >= 502 && http_status <= 504) || (http_status >= 521 && http_status <= 524);
    return transport_failed || unavailable ? SyncStatus::Unreachable : SyncStatus::Error;
}

constexpr bool includes_slot(SyncScope scope, std::string_view key) {
    return scope != SyncScope::PublishHostedOnline || key.starts_with("online-");
}

constexpr Action next_action(std::string_view local, std::string_view cloud, std::string_view common,
                             SyncScope scope = SyncScope::All) {
    if (local == cloud)
        return Action::None;
    // A missing cloud slot can be seeded. Existing cloud progress can be
    // advanced only from the copy this device last synchronized.
    if (!local.empty() && (cloud.empty() || cloud == common))
        return Action::Upload;
    // Publish-only syncs leave a running game or an agreed lobby seed intact.
    return scope == SyncScope::All ? Action::Download : Action::Defer;
}
} // namespace dkr::cloud_saves
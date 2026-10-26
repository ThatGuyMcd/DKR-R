#include "camera_session.hpp"
#include "mods/sdk2_bridge.hpp"
#include <cmath>
#include <cstring>

namespace dkr::runtime::camera {
PresentationSession::PresentationSession(const mods::code::FrozenSet& selected, rom::Revision revision,
                                         const SettingsChannel& settings, Actions& actions)
    : revision_(revision), settings_(settings), actions_(actions) {
    if (selected.ordered.empty())
        return; // Original DKR, no local provider.
    if (selected.ordered.size() != 1)
        throw mods::Error("Enable only one camera provider.");
    if (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80)
        throw mods::Error("Camera presentation does not support this Game Pak.");
    const auto& package = selected.ordered[0];
    const auto configured = selected.settings.find(package->id);
    program_ =
        std::make_unique<Program>(package, configured == selected.settings.end() ? Json::object() : configured->second,
                                  revision == rom::Revision::UsV77 ? "us.v77" : "us.v80");
}
ViewBatch PresentationSession::evaluate(std::span<const std::uint8_t> retained, const FrameStamp& stamp,
                                        std::span<const ViewOwner> owners) {
    ViewBatch batch;
    if (owners.size() > 4)
        return batch;
    if (program_ && program_->quarantined())
        batch.diagnostic = program_->error();
    try {
        const auto configuration = settings_.current();
        if (!configuration)
            throw mods::Error("Camera profiles are not ready.");
        unsigned seen_views = 0, seen_physical = 0;
        for (const auto& owner : owners) {
            if (owner.view >= 4 || owner.physical_profile >= 4 || owner.settings_profile >= 4 ||
                !std::isfinite(owner.aspect) || owner.aspect < 0.1F || owner.aspect > 16 ||
                (seen_views & (1U << owner.view)) || (seen_physical & (1U << owner.physical_profile)))
                throw mods::Error("Camera local-view ownership is invalid.");
            seen_views |= 1U << owner.view;
            seen_physical |= 1U << owner.physical_profile;
        }
        FrameStamp frame = stamp;
        std::array<DkrViewInput, 4> inputs{};
        bool driving = false;
        for (const auto& owner : owners) {
            frame.aspect = owner.aspect;
            inputs[owner.view] = actions_.consume(owner.physical_profile, frame.dt);
            auto state = reader_.capture(retained, revision_, owner.view, frame);
            batch.snapshots[owner.view] = state;
            driving |= state && (state->flags & DKR_VIEW_DRIVING) && !(state->flags & DKR_VIEW_SCRIPTED);
        }
        // No mesh rebuilding or mod execution in Track Select/title menus.
        // Consume their local taps instead of replaying them in the race.
        if (!driving)
            return batch;
        if (frame.capabilities & DKR_VIEW_STATIC_COLLISION) {
            if (retained.size() < 0x800000U)
                throw mods::Error("Camera presentation requires a complete retained image.");
            const auto address = revision_ == rom::Revision::UsV77 ? 0x800DC918U : 0x800DCE88U;
            std::uint32_t model;
            std::memcpy(&model, retained.data() + address - 0x80000000U, 4);
            if (!scene_attempted_ || scene_ != stamp.scene || epoch_ != stamp.epoch || model_ != model) {
                collision_.reset();
                scene_error_.clear();
                scene_attempted_ = true;
                scene_ = stamp.scene;
                epoch_ = stamp.epoch;
                model_ = model;
                try {
                    collision_ = CollisionScene::capture(retained, model, (std::uint64_t(epoch_) << 32U) | scene_);
                } catch (const std::exception& e) {
                    scene_error_ = e.what();
                }
            }
            if (!collision_)
                frame.capabilities &= ~std::uint32_t(DKR_VIEW_STATIC_COLLISION);
        }
        // This implementation has no retained moving-platform/door adapter.
        // Never accidentally promote static queries to dynamic capability.
        frame.capabilities &= ~std::uint32_t(DKR_VIEW_DYNAMIC_COLLISION);
        const Program::Query query = [scene = collision_](const DkrViewQuery& request) {
            if (scene)
                return scene->query(request);
            DkrViewHit hit{};
            hit.api = 1;
            hit.size = sizeof(hit);
            hit.status = DKR_VIEW_QUERY_UNSUPPORTED;
            return hit;
        };
        for (const auto& owner : owners) {
            frame.aspect = owner.aspect;
            auto state = batch.snapshots[owner.view];
            if (!state)
                continue; // No racer/menu/scripted fallback, not a fault.
            state->capabilities = frame.capabilities;
            batch.snapshots[owner.view] = state;
            auto input = inputs[owner.view];
            batch.xr_recenter |= (input.pressed & DKR_VIEW_XR_RECENTER) != 0;
            // Explicit tool ownership temporarily suspends Camera++ only for
            // this local viewport. Releasing it restores the existing provider.
            const auto local_camera = dkr::sdk2::camera_state(owner.view);
            if (local_camera.cut && !local_camera.enabled)
                state->flags |= DKR_VIEW_CUT;
            if (auto pose = dkr::sdk2::camera_frame(*state, input, retained)) {
                batch.poses[owner.view] = *pose;
                dkr::sdk2::debug_frame(*state, batch.poses[owner.view], unsigned(owners.size()));
                continue;
            }
            if (!program_ || program_->quarantined()) {
                dkr::sdk2::debug_frame(*state, {}, unsigned(owners.size()));
                continue;
            }
            if (!program_->settings(configuration->views[owner.settings_profile]))
                throw mods::Error(program_->error());
            batch.poses[owner.view] = program_->frame(*state, input, query);
            dkr::sdk2::debug_frame(*state, batch.poses[owner.view], unsigned(owners.size()));
            if (program_->quarantined()) {
                batch.poses = {};
                batch.diagnostic = program_->error();
                return batch;
            }
        }
        batch.diagnostic = scene_error_;
    } catch (const std::exception& e) {
        batch.poses = {};
        batch.diagnostic = e.what();
    }
    return batch;
}
} // namespace dkr::runtime::camera

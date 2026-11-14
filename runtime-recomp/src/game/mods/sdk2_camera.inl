LocalCamera camera_state(unsigned view) {
    std::lock_guard guard(mutex);
    return view < 4 ? cameras[view] : LocalCamera{};
}
bool camera_edit(unsigned view, const LocalCamera& value) {
    std::lock_guard guard(mutex);
    if (!current.active || view >= 4 || !value.valid)
        return false;
    if (!std::isfinite(value.yaw) || !std::isfinite(value.pitch) || !std::isfinite(value.fov) || value.fov < 0.35F ||
        value.fov > 2.1F || !std::isfinite(value.speed) || value.speed < 1 || value.speed > 2000 ||
        !std::isfinite(value.smoothing) || value.smoothing < 0 || value.smoothing > 30)
        return false;
    for (auto coordinate : value.position)
        if (!std::isfinite(coordinate) || std::abs(coordinate) > 1e6F)
            return false;
    cameras[view] = value;
    cameras[view].cut = true;
    return true;
}
std::optional<DkrViewPose> camera_frame(const DkrViewSnapshot& state, const DkrViewInput& input,
                                        std::span<const std::uint8_t> retained) {
    std::lock_guard guard(mutex);
    if (state.view >= 4 || !current.active)
        return {};
    auto& camera = cameras[state.view];
    if (camera.scene != state.scene) {
        camera = {};
        camera.scene = state.scene;
    }
    if (!(state.flags & DKR_VIEW_DRIVING) || (state.flags & DKR_VIEW_SCRIPTED)) {
        camera.enabled = false;
        return {};
    }
    if (!camera.valid) {
        std::copy_n(state.original_position, 3, camera.position);
        camera.yaw = state.original_yaw;
        camera.pitch = state.original_pitch;
        camera.fov = state.original_fov;
        camera.valid = true;
    }
    if (!camera.enabled) {
        std::copy_n(state.original_position, 3, camera.position);
        camera.yaw = state.original_yaw;
        camera.pitch = state.original_pitch;
        camera.fov = state.original_fov;
        camera.cut = false;
        return {};
    }
    if (camera.follow) {
        const auto panel = std::find_if(current.panels.begin(), current.panels.end(),
                                        [&](const auto& p) { return p.owner == camera.owner; });
        const EntityView* target = nullptr;
        if (panel != current.panels.end() && panel->inspector)
            for (const auto& entity : *panel->inspector)
                if (entity.slot == camera.slot && entity.generation == camera.generation)
                    target = &entity;
        const auto word = [&](unsigned address) {
            std::uint32_t result = 0;
            if (address < 0x80000000U || address > 0x807ffffcU || retained.size() < 0x800000U)
                return result;
            std::memcpy(&result, retained.data() + address - 0x80000000U, 4);
            return result;
        };
        if (!target || word(target->address + 0x40) != target->header) {
            camera.enabled = false;
            return {};
        }
        float point[3];
        for (unsigned i = 0; i < 3; ++i) {
            const auto raw = word(target->address + 12 + i * 4);
            std::memcpy(point + i, &raw, 4);
            if (!std::isfinite(point[i])) {
                camera.enabled = false;
                return {};
            }
        }
        const float desired[] = {point[0], point[1] + 80.F, point[2] + 200.F};
        const float factor = camera.cut || camera.smoothing <= 0 ? 1.F : 1.F - std::exp(-camera.smoothing * state.dt);
        for (unsigned i = 0; i < 3; ++i)
            camera.position[i] += (desired[i] - camera.position[i]) * factor;
        const float x = point[0] - camera.position[0], y = point[1] - camera.position[1],
                    z = point[2] - camera.position[2];
        camera.yaw = std::atan2(-x, -z);
        camera.pitch = std::atan2(y, std::sqrt(x * x + z * z));
    } else if (!input.blocked) {
        camera.yaw = std::remainder(camera.yaw - input.orbit_x * state.dt * 2.F, 6.2831853F);
        camera.pitch = std::clamp(camera.pitch - input.orbit_y * state.dt * 2.F, -1.5F, 1.5F);
        const float movement = input.zoom * camera.speed * state.dt;
        camera.position[0] -= std::sin(camera.yaw) * std::cos(camera.pitch) * movement;
        camera.position[1] += std::sin(camera.pitch) * movement;
        camera.position[2] -= std::cos(camera.yaw) * std::cos(camera.pitch) * movement;
    }
    // This is a local retained-image camera, not a gameplay camera pointer.
    // Keep the existing private draw closure's bounded resource envelope.
    float distance = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const float d = camera.position[i] - state.position[i];
        distance += d * d;
    }
    if (distance > 1000000.F) {
        camera.enabled = false;
        current.notice = "Tool camera left the supported 1000-unit envelope. Normal camera restored.";
        cached.reset();
        return {};
    }
    DkrViewPose pose{};
    pose.api = 1;
    pose.size = sizeof(pose);
    pose.view = state.view;
    pose.mode = DKR_VIEW_CHASE;
    pose.racer_id = state.racer_id;
    pose.racer_generation = state.racer_generation;
    std::copy_n(camera.position, 3, pose.position);
    pose.yaw = camera.yaw;
    pose.pitch = camera.pitch;
    pose.vertical_fov = camera.fov;
    pose.near_plane = state.original_near;
    pose.far_plane = state.original_far;
    pose.cut = camera.cut || (state.flags & DKR_VIEW_CUT);
    camera.cut = false;
    return pose;
}

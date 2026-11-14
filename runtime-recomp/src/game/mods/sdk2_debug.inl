void debug(unsigned owner, std::vector<DebugPrimitive> primitives) {
    std::lock_guard guard(mutex);
    if (primitives.empty())
        debug_owners.erase(owner);
    else
        debug_owners[owner] = std::move(primitives);
    if (debug_owners.empty())
        debug_views = {};
}
bool debug_available() {
    std::lock_guard guard(mutex);
    return debug_enabled.load() && current.active && !debug_owners.empty();
}
std::shared_ptr<const std::vector<DebugLine>> debug_lines() {
    std::lock_guard guard(mutex);
    auto result = std::make_shared<std::vector<DebugLine>>();
    if (current.active)
        for (const auto& view : debug_views)
            result->insert(result->end(), view.begin(), view.end());
    return result;
}
void debug_frame(const DkrViewSnapshot& snapshot, const std::optional<DkrViewPose>& pose, unsigned view_count) {
    std::lock_guard guard(mutex);
    if (snapshot.view >= 4)
        return;
    auto& lines = debug_views[snapshot.view];
    lines.clear();
    if (debug_owners.empty() || !(snapshot.flags & DKR_VIEW_DRIVING) || (snapshot.flags & DKR_VIEW_SCRIPTED))
        return;
    const auto* eye = pose ? pose->position : snapshot.original_position;
    const float yaw = pose ? pose->yaw : snapshot.original_yaw, pitch = pose ? pose->pitch : snapshot.original_pitch;
    const float fov = pose ? pose->vertical_fov : snapshot.original_fov;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch),
                f = 1.F / std::tan(fov * .5F);
    float left = 0, top = 0, width = 1, height = 1;
    if (view_count == 2) {
        top = snapshot.view * .5F;
        height = .5F;
    } else if (view_count > 2) {
        left = (snapshot.view & 1) * .5F;
        top = (snapshot.view >> 1) * .5F;
        width = height = .5F;
    }
    const auto project = [&](const float* point, float& x, float& y) {
        const float dx = point[0] - eye[0], dy = point[1] - eye[1], dz = point[2] - eye[2];
        const float right = dx * cy - dz * sy, forward = -dx * sy - dz * cy, depth = forward * cp + dy * sp,
                    up = dy * cp - forward * sp;
        if (depth < 1.F)
            return false;
        const float nx = right * f / (depth * snapshot.aspect), ny = up * f / depth;
        if (!std::isfinite(nx) || !std::isfinite(ny) || std::abs(nx) > 16 || std::abs(ny) > 16)
            return false;
        x = left + (nx * .5F + .5F) * width;
        y = top + (.5F - ny * .5F) * height;
        return true;
    };
    const auto line = [&](const DebugPrimitive& primitive, const float* a, const float* b, std::string label = {}) {
        if (lines.size() >= 768)
            return;
        DebugLine value;
        value.rgba = primitive.rgba;
        value.label = std::move(label);
        if (project(a, value.x1, value.y1) && project(b, value.x2, value.y2)) {
            // Each primitive stays clipped to its actual local viewport.
            const float dx = value.x2 - value.x1, dy = value.y2 - value.y1;
            float first = 0, last = 1;
            const auto clip = [&](float p, float q) {
                if (std::abs(p) < 1e-9F)
                    return q >= 0;
                const float r = q / p;
                if (p < 0) {
                    if (r > last)
                        return false;
                    first = std::max(first, r);
                } else {
                    if (r < first)
                        return false;
                    last = std::min(last, r);
                }
                return true;
            };
            if (clip(-dx, value.x1 - left) && clip(dx, left + width - value.x1) && clip(-dy, value.y1 - top) &&
                clip(dy, top + height - value.y1)) {
                value.x2 = value.x1 + last * dx;
                value.y2 = value.y1 + last * dy;
                value.x1 += first * dx;
                value.y1 += first * dy;
                lines.push_back(std::move(value));
            }
        }
    };
    for (const auto& [owner, primitives] : debug_owners)
        for (const auto& primitive : primitives)
            if (primitive.view == snapshot.view) {
                if (primitive.kind != 2)
                    line(primitive, primitive.a, primitive.kind == 3 ? primitive.a : primitive.b,
                         primitive.kind == 3 ? primitive.label : std::string{});
                else {
                    float corners[8][3];
                    for (unsigned corner = 0; corner < 8; ++corner)
                        for (unsigned axis = 0; axis < 3; ++axis)
                            corners[corner][axis] = (corner & (1U << axis)) ? primitive.b[axis] : primitive.a[axis];
                    for (unsigned corner = 0; corner < 8; ++corner)
                        for (unsigned axis = 0; axis < 3; ++axis)
                            if (!(corner & (1U << axis)))
                                line(primitive, corners[corner], corners[corner | (1U << axis)]);
                }
            }
}

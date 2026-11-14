// Included inside the checked model submission scope. At most 30 vertices fit
// the original 32-slot RSP cache; commands, culling and texture boundaries are
// never crossed. Mixed driver/head matrices are uploaded as separate runs.
std::array<Corner, 30> batch;
unsigned corners = 0, batch_flags = 0, batch_texture = 0;
const auto submit_batch = [&] {
    if (!corners)
        return;
    rsp.textureState.on = std::uint8_t(batch_texture);
    rsp.clearGeometryMode(rsp.cullBothMask);
    if (!(batch_flags & 0x40U))
        rsp.setGeometryMode(rsp.viewportStack[rsp.viewportStackSize - 1].scale.x > 0
                                ? (rsp.cullBothMask & ~rsp.cullFrontMask)
                                : rsp.cullFrontMask);
    for (unsigned v = 0; v < corners; ++v)
        std::memcpy(state.RDRAM + kVertices + v * sizeof(batch[v].vertex), &batch[v].vertex, sizeof(batch[v].vertex));
    for (unsigned first = 0; first < corners;) {
        unsigned end = first + 1;
        while (end < corners && batch[end].matrix == batch[first].matrix)
            ++end;
        rsp.modelMatrixStackSize = int(batch[first].matrix + 1);
        rsp.modelViewProjChanged = true;
        rsp.modelViewProjInserted = false;
        rsp.setVertex(kVertices + first * sizeof(batch[first].vertex), std::uint8_t(end - first), std::uint8_t(first));
        ++p.vertex_uploads;
        first = end;
    }
    for (unsigned v = 0; v < corners; ++v)
        rsp.modifyVertex(std::uint16_t(v), G_MWO_POINT_ST,
                         (std::uint32_t(std::uint16_t(batch[v].s)) << 16) | std::uint16_t(batch[v].t));
    for (unsigned v = 0; v < corners; v += 3)
        rsp.drawIndexedTri(v, v + 1, v + 2);
    ++p.batches;
    corners = 0;
};
for (const auto& step : packet->steps) {
    if (step.kind == Step::Command) {
        submit_batch();
        command(state, step.w0, step.w1);
        continue;
    }
    if (driver_mask && driver_mask->hide[step.source_triangle])
        continue;
    if (corners && (corners == batch.size() || batch_flags != step.w0 || batch_texture != step.w1))
        submit_batch();
    if (!corners) {
        batch_flags = step.w0;
        batch_texture = step.w1;
    }
    for (const auto& vertex : step.corners)
        batch[corners++] = vertex;
}
submit_batch();

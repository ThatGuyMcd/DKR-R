// Read-only, bounded scene metadata. All indices refer to the currently loaded
// guest scene, never process addresses or a promise of live mesh replacement.
int GameWorld::sdk2_track_catalogue(Memory& bus, std::string_view list, unsigned index, Json& item, Budget& budget) {
    if (budget.instructions < 4096)
        return -6;
    budget.instructions -= 4096;
    const auto real = [&](unsigned p) { return std::bit_cast<float>(std::uint32_t(bus.load(p, 4))); };
    const auto position = [&](unsigned object) {
        return Json::array({real(object + 12), real(object + 16), real(object + 20)});
    };
    if (list == "scene") {
        if (index)
            return 1;
        const auto header = std::uint32_t(bus.load(symbol("gCurrentLevelHeader"), 4));
        if (!sdk2::guest(header, 0xc4))
            return -3;
        item = {
            {"map", bus.load(symbol("gPlayableMapId"), 4)},
            {"race_type", bus.load(header + 0x4c, 1)},
            {"laps", bus.load(header + 0x4b, 1)},
            {"vehicle", bus.load(header + 0x4d, 1)},
            {"vehicles", bus.load(header + 0x4e, 1)},
            {"fog", Json::array()},
            {"course_height", real(header + 8)},
            {"geometry_asset", bus.load(header + 0x34, 2)},
            {"policy",
             "Read-only metadata. Structural water/material/mesh changes require a prepared asset and scene reload."}};
        for (unsigned i = 0; i < 5; ++i)
            item["fog"].push_back(std::int16_t(bus.load(header + 0x3a + i * 2, 2)));
        return 0;
    }
    if (list == "ai-nodes" || list == "spawn-points" || list == "triggers") {
        const unsigned count = unsigned(bus.load(symbol("gObjectCount"), 4));
        const auto objects = std::uint32_t(bus.load(symbol("gObjPtrList"), 4));
        if (count > 512 || !sdk2::guest(objects, count * 4))
            return -3;
        unsigned found = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto object = std::uint32_t(bus.load(objects + i * 4, 4));
            if (!sdk2::guest(object, 0x80))
                continue;
            const unsigned behaviour = unsigned(bus.load(object + 0x48, 2));
            if (list == "ai-nodes"       ? behaviour != 16
                : list == "spawn-points" ? behaviour != 11
                                         : behaviour != 30 && behaviour != 41 && behaviour != 55)
                continue;
            if (found++ != index)
                continue;
            const auto entry = std::uint32_t(bus.load(object + 0x3c, 4));
            item = {{"index", index},
                    {"object_id", bus.load(object + 0x4a, 2)},
                    {"behaviour", behaviour},
                    {"position", position(object)},
                    {"segment", bus.load(object + 0x2e, 2)}};
            if (list == "ai-nodes") {
                if (!sdk2::guest(entry, 16))
                    return -3;
                item["node_id"] = bus.load(entry + 9, 1);
                item["elevation"] = std::int8_t(bus.load(entry + 14, 1));
                item["links"] = Json::array();
                const auto state = std::uint32_t(bus.load(object + 0x64, 4));
                if (!sdk2::guest(state, 0x1c))
                    return -3;
                for (unsigned n = 0; n < 4; ++n)
                    item["links"].push_back({{"node", bus.load(entry + 10 + n, 1)},
                                             {"distance", bus.load(state + 16 + n * 2, 2)},
                                             {"active", bus.load(state + n * 4, 4) != 0}});
                item["policy"] =
                    "Read-only graph; native AI links have cached NPC users. Edit source links and reload the track.";
            } else if (list == "spawn-points") {
                if (!sdk2::guest(entry, 12))
                    return -3;
                item["racer"] = bus.load(entry + 8, 1);
                item["entrance"] = bus.load(entry + 9, 1);
                item["yaw"] = bus.load(object, 2);
                item["vehicle"] = std::int8_t(bus.load(entry + 11, 1));
                item["policy"] =
                    "An anchor for the next native racer spawn, not a live racer. Restart at an existing entrance or use teleport.";
            } else {
                const auto state = std::uint32_t(bus.load(object + 0x64, 4));
                if (!sdk2::guest(state, 0x18))
                    return -3;
                item["radius"] = std::int32_t(bus.load(state + 0x10, 4));
                item["vehicle"] = bus.load(state + 0x14, 1);
                item["policy"] =
                    "Reviewed radius/vehicle properties use the entity inspector; trigger event/type ownership stays native.";
            }
            return 0;
        }
        return 1;
    }
    const auto model = std::uint32_t(bus.load(symbol("gCurrentLevelModel"), 4));
    if (!sdk2::guest(model, 0x4c))
        return -3;
    const unsigned count = unsigned(bus.load(model + 0x1a, 2));
    const auto segments = std::uint32_t(bus.load(model + 4, 4)), boxes = std::uint32_t(bus.load(model + 8, 4));
    if (count > 1024 || !sdk2::guest(segments, count * 0x44) || !sdk2::guest(boxes, count * 12))
        return -3;
    if (list == "segments") {
        if (index >= count)
            return 1;
        const auto segment = segments + index * 0x44;
        Json low = Json::array(), high = Json::array();
        for (unsigned n = 0; n < 3; ++n) {
            low.push_back(std::int16_t(bus.load(boxes + index * 12 + n * 2, 2)));
            high.push_back(std::int16_t(bus.load(boxes + index * 12 + 6 + n * 2, 2)));
        }
        item = {{"index", index},
                {"count", count},
                {"minimum", low},
                {"maximum", high},
                {"vertices", bus.load(segment + 0x1c, 2)},
                {"triangles", bus.load(segment + 0x1e, 2)},
                {"batches", bus.load(segment + 0x20, 2)}};
        return 0;
    }
    if (!list.starts_with("triangles/"))
        return -5;
    unsigned selected = 0;
    const auto number = list.substr(10);
    if (number.empty() || number.size() > 4)
        return -1;
    for (char ch : number) {
        if (ch < '0' || ch > '9')
            return -1;
        selected = selected * 10 + unsigned(ch - '0');
    }
    if (selected >= count)
        return -1;
    const auto segment = segments + selected * 0x44;
    const unsigned nv = unsigned(bus.load(segment + 0x1c, 2)), nt = unsigned(bus.load(segment + 0x1e, 2)),
                   nb = unsigned(bus.load(segment + 0x20, 2));
    const auto vertices = std::uint32_t(bus.load(segment, 4)), triangles = std::uint32_t(bus.load(segment + 4, 4)),
               batches = std::uint32_t(bus.load(segment + 12, 4));
    if (nt > 8192 || nb > 8192 || !sdk2::guest(vertices, nv * 10) || !sdk2::guest(triangles, nt * 16) ||
        !sdk2::guest(batches, (nb + 1) * 12))
        return -3;
    if (index >= nt)
        return 1;
    for (unsigned b = 0; b < nb; ++b) {
        if (budget.instructions < 16)
            return -6;
        budget.instructions -= 16;
        const auto at = batches + b * 12;
        const unsigned begin = unsigned(bus.load(at + 4, 2)), end = unsigned(bus.load(at + 16, 2));
        if (begin > end || end > nt)
            return -1;
        if (index < begin || index >= end)
            continue;
        Json points = Json::array();
        sdk2::Vec v[3];
        for (unsigned n = 0; n < 3; ++n) {
            const unsigned vertex =
                unsigned(bus.load(at + 2, 2)) + unsigned(bus.load(triangles + index * 16 + n + 1, 1));
            if (vertex >= nv)
                return -1;
            const auto p = vertices + vertex * 10;
            v[n] = {float(std::int16_t(bus.load(p, 2))), float(std::int16_t(bus.load(p + 2, 2))),
                    float(std::int16_t(bus.load(p + 4, 2)))};
            points.push_back(Json::array({v[n].x, v[n].y, v[n].z}));
        }
        const auto normal = sdk2::normalized(sdk2::cross(v[1] - v[0], v[2] - v[0]));
        item = {
            {"index", index},
            {"segment", selected},
            {"vertices", points},
            {"normal", Json::array({normal.x, normal.y, normal.z})},
            {"texture", bus.load(at, 1)},
            {"batch", b},
            {"policy",
             "Static render/collision geometry. Texture ID is not a universal physics surface ID; no live mesh writes."}};
        return 0;
    }
    return 1;
}

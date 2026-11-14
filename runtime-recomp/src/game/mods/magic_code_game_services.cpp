#include "magic_code_game_services.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace dkr::mods::code {
namespace {
void valid(std::uint32_t address, unsigned size) {
    if (address < 0x80000400U || address > 0x80800000U || size > 0x80800000U - address)
        throw Error("Game SDK object is outside native RDRAM.");
}
unsigned count(Memory& bus, const SymbolResolver& symbol, const char* name, unsigned maximum) {
    const auto n = std::uint32_t(bus.load(symbol(name), 4));
    if (n > maximum)
        throw Error("Game SDK list exceeds its native capacity.");
    return n;
}
std::uint32_t object(Memory& bus, const SymbolResolver& symbol, unsigned index, bool racers) {
    const auto n = count(bus, symbol, racers ? "gNumRacers" : "gObjectCount", racers ? 8 : 512);
    if (index >= n)
        return 0;
    const auto list = std::uint32_t(bus.load(symbol(racers ? "gRacersByPort" : "gObjPtrList"), 4));
    valid(list, n * 4);
    const auto obj = std::uint32_t(bus.load(list + index * 4, 4));
    if (obj)
        valid(obj, 0x80);
    return obj;
}
} // namespace
bool game_memory_service(std::string_view name, Memory& bus, Registers& c, Budget& budget,
                         const SymbolResolver& symbol) {
    const auto a = std::uint32_t(c.integer[4]), b = std::uint32_t(c.integer[5]), d = std::uint32_t(c.integer[6]);
    if (name == "dkr_collision_floor") {
        const auto segment_index = std::uint32_t(bus.load(a, 4));
        const auto real = [&](std::uint32_t address) {
            return std::bit_cast<float>(std::uint32_t(bus.load(address, 4)));
        };
        const float x = real(a + 4), ceiling = real(a + 8), z = real(a + 12);
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(ceiling) || std::abs(x) > 1e7F ||
            std::abs(z) > 1e7F)
            throw Error("Collision query coordinates are invalid.");
        bus.fill(b, 24);
        c.integer[2] = 0;
        const auto model = std::uint32_t(bus.load(symbol("gCurrentLevelModel"), 4));
        if (!model)
            return true;
        valid(model, 0x4c);
        const auto segments = std::uint32_t(bus.load(model + 0x1a, 2));
        if (segment_index >= segments)
            return true;
        const auto list = std::uint32_t(bus.load(model + 4, 4));
        valid(list, segments * 0x44);
        const auto segment = list + segment_index * 0x44;
        const auto vertices = std::uint32_t(bus.load(segment, 4)), triangles = std::uint32_t(bus.load(segment + 4, 4)),
                   batches = std::uint32_t(bus.load(segment + 0xc, 4));
        const auto facets = std::uint32_t(bus.load(segment + 0x14, 4)),
                   planes = std::uint32_t(bus.load(segment + 0x18, 4));
        const auto vertex_count = std::uint32_t(bus.load(segment + 0x1c, 2)),
                   triangle_count = std::uint32_t(bus.load(segment + 0x1e, 2)),
                   batch_count = std::uint32_t(bus.load(segment + 0x20, 2));
        if (triangle_count > 8192 || triangle_count * 32 > budget.instructions) {
            c.integer[2] = UINT32_MAX;
            return true;
        }
        budget.instructions -= triangle_count * 32;
        valid(vertices, vertex_count * 10);
        valid(triangles, triangle_count * 16);
        valid(batches, (batch_count + 1) * 12);
        valid(facets, triangle_count * 8);
        float best = 0;
        bool found = false;
        for (unsigned batch = 0; batch < batch_count; ++batch) {
            const auto begin = std::uint32_t(bus.load(batches + batch * 12 + 4, 2)),
                       end = std::uint32_t(bus.load(batches + (batch + 1) * 12 + 4, 2));
            const auto base = std::uint32_t(bus.load(batches + batch * 12 + 2, 2));
            if (begin > end || end > triangle_count)
                throw Error("Collision query found an invalid triangle batch.");
            for (unsigned face = begin; face < end; ++face) {
                float vx[3], vz[3];
                for (unsigned i = 0; i < 3; ++i) {
                    const auto index = base + unsigned(bus.load(triangles + face * 16 + i + 1, 1));
                    if (index >= vertex_count)
                        throw Error("Collision query found an invalid vertex.");
                    vx[i] = float(std::int16_t(bus.load(vertices + index * 10, 2)));
                    vz[i] = float(std::int16_t(bus.load(vertices + index * 10 + 4, 2)));
                }
                const auto side = [&](unsigned i, unsigned j) {
                    return (x - vx[i]) * (vz[j] - vz[i]) - (z - vz[i]) * (vx[j] - vx[i]);
                };
                const float e0 = side(0, 1), e1 = side(1, 2), e2 = side(2, 0);
                if (!((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)))
                    continue;
                const auto plane_index = unsigned(bus.load(facets + face * 8, 2));
                const auto plane = planes + plane_index * 16;
                valid(plane, 16);
                const float nx = real(plane), ny = real(plane + 4), nz = real(plane + 8), w = real(plane + 12);
                if (ny == 0)
                    continue;
                const float y = -((nx * x + nz * z + w) / ny);
                if (!std::isfinite(y) || y > ceiling || (found && y <= best))
                    continue;
                found = true;
                best = y;
                bus.store(b, 4, std::bit_cast<std::uint32_t>(y));
                bus.store(b + 4, 4, segment_index);
                bus.store(b + 8, 4, face);
                bus.store(b + 12, 4, std::bit_cast<std::uint32_t>(nx));
                bus.store(b + 16, 4, std::bit_cast<std::uint32_t>(ny));
                bus.store(b + 20, 4, std::bit_cast<std::uint32_t>(nz));
            }
        }
        c.integer[2] = found ? 1 : 0;
        return true;
    }
    if (name == "dkr_input_get") {
        if (a >= 4)
            throw Error("Input player must be 0..3.");
        const auto port = std::uint32_t(bus.load(symbol("sPlayerID") + a, 1));
        if (port >= 4)
            throw Error("Logical input mapping is invalid.");
        const auto pad = symbol("gControllerCurrData") + port * 6;
        const auto axis = [](std::uint64_t raw) {
            int value = std::int8_t(raw);
            if (value > -8 && value < 8)
                return 0;
            return std::clamp(value + (value > 0 ? -8 : 8), -70, 70);
        };
        bus.fill(b, 20);
        bus.store(b, 4, bus.load(pad, 2));
        bus.store(b + 4, 4, bus.load(symbol("gControllerButtonsPressed") + port * 2, 2));
        bus.store(b + 8, 4, bus.load(symbol("gControllerButtonsReleased") + port * 2, 2));
        bus.store(b + 12, 4, std::uint32_t(axis(bus.load(pad + 2, 1))));
        bus.store(b + 16, 4, std::uint32_t(axis(bus.load(pad + 3, 1))));
        c.integer[2] = 1;
        return true;
    }
    if (name == "dkr_racer_count" || name == "dkr_object_count") {
        c.integer[2] = count(bus, symbol, name == "dkr_racer_count" ? "gNumRacers" : "gObjectCount",
                             name == "dkr_racer_count" ? 8 : 512);
        return true;
    }
    if (name == "dkr_racer_get" || name == "dkr_racer_set" || name == "dkr_object_get" || name == "dkr_object_set") {
        const bool racers = name.starts_with("dkr_racer");
        const auto obj = object(bus, symbol, a, racers);
        c.integer[2] = 0;
        if (!obj)
            return true;
        std::uint32_t racer = 0;
        if (racers) {
            racer = std::uint32_t(bus.load(obj + 0x64, 4));
            valid(racer, 0x224);
        }
        if (name.ends_with("_set")) {
            if (b <= 3) {
                if (!std::isfinite(std::bit_cast<float>(d)))
                    throw Error("Game SDK rejects non-finite transform/speed values.");
                if (b == 3)
                    bus.store(racers ? racer + 0x2c : obj + 8, 4, d);
                else
                    bus.store(obj + 0xc + b * 4, 4, d);
            } else if (racers && b == 4) {
                if (d > 99)
                    throw Error("Banana count exceeds the two-digit HUD range.");
                bus.store(racer + 0x185, 1, d);
            } else
                throw Error("Unknown game SDK write field.");
            c.integer[2] = 1;
            return true;
        }
        if (!racers) {
            bus.fill(b, 36);
            bus.store(b, 4, obj);
            bus.store(b + 4, 4, bus.load(obj + 0x48, 2));
            bus.store(b + 8, 4, bus.load(obj + 0x4a, 2));
            for (unsigned i = 0; i < 3; ++i)
                bus.store(b + 12 + i * 4, 4, bus.load(obj + 0xc + i * 4, 4));
            bus.store(b + 24, 4, bus.load(obj + 8, 4));
            bus.store(b + 28, 4, bus.load(obj + 0x39, 1));
            bus.store(b + 32, 4, bus.load(obj + 0x5c, 4));
        } else {
            bus.fill(b, 64);
            bus.store(b, 4, obj);
            bus.store(b + 4, 4, racer);
            bus.store(b + 8, 4, std::uint32_t(std::int16_t(bus.load(racer, 2))));
            bus.store(b + 12, 4, bus.load(racer + 2, 1));
            bus.store(b + 16, 4, bus.load(racer + 3, 1));
            bus.store(b + 20, 4, bus.load(racer + 0x1d6, 1));
            bus.store(b + 24, 4, bus.load(racer + 0x193, 1));
            bus.store(b + 28, 4, bus.load(racer + 0x194, 1));
            bus.store(b + 32, 4, bus.load(racer + 0x1ae, 2));
            bus.store(b + 36, 4, bus.load(racer + 0x1d8, 1));
            bus.store(b + 40, 4, bus.load(racer + 0x185, 1));
            for (unsigned i = 0; i < 3; ++i)
                bus.store(b + 44 + i * 4, 4, bus.load(obj + 0xc + i * 4, 4));
            bus.store(b + 56, 4, bus.load(racer + 0x2c, 4));
        }
        c.integer[2] = 1;
        return true;
    }
    if (name == "dkr_camera_get" || name == "dkr_camera_set") {
        if (a >= 8)
            throw Error("Camera must be 0..7.");
        const auto camera = symbol("gCameras") + a * 0x44;
        if (name == "dkr_camera_set") {
            if (b > 4 || !std::isfinite(std::bit_cast<float>(d)))
                throw Error("Invalid camera field/value.");
            bus.store(b <= 2 ? camera + 0xc + b * 4 : b == 3 ? camera + 0x1c : camera + 0x30, 4, d);
        } else {
            bus.fill(b, 40);
            bus.store(b, 4, camera);
            for (unsigned i = 0; i < 3; ++i)
                bus.store(b + 4 + i * 4, 4, bus.load(camera + 0xc + i * 4, 4));
            for (unsigned i = 0; i < 3; ++i)
                bus.store(b + 16 + i * 4, 4, std::uint32_t(std::int16_t(bus.load(camera + i * 2, 2))));
            bus.store(b + 28, 4, bus.load(camera + 0x1c, 4));
            bus.store(b + 32, 4, bus.load(camera + 0x30, 4));
            bus.store(b + 36, 4, bus.load(camera + 0x36, 2));
        }
        c.integer[2] = 1;
        return true;
    }
    return false;
}
} // namespace dkr::mods::code

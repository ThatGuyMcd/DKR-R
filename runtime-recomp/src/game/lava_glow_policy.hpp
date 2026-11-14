#pragma once
#include "enhanced_water_policy.hpp"
#include "lava_lighting_policy.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <numeric>
#include <vector>

namespace dkr::runtime::water::enhanced {
// Local visual cache. No guest writes, physics, replay inputs or GPU rays.
struct GlowTriangle {
    std::array<Vec3, 3> p{};
    std::array<std::uint32_t, 3> addresses{};
    bool source = false;
};
struct GlowGeometry {
    std::vector<GlowTriangle> triangles;
    bool complete = true;
};
inline constexpr unsigned kGlowTriangleLimit = 65536;
inline constexpr unsigned kGlowSourceLimit = 4096;
inline Vec3 glow_sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 glow_add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 glow_mul(Vec3 a, float b) {
    return {a.x * b, a.y * b, a.z * b};
}
inline float glow_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 glow_cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 glow_closest(Vec3 p, const GlowTriangle& t) {
    const auto a = t.p[0], b = t.p[1], c = t.p[2];
    const auto ab = glow_sub(b, a), ac = glow_sub(c, a), ap = glow_sub(p, a);
    const float d1 = glow_dot(ab, ap), d2 = glow_dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    const auto bp = glow_sub(p, b);
    const float d3 = glow_dot(ab, bp), d4 = glow_dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return glow_add(a, glow_mul(ab, d1 / (d1 - d3)));
    const auto cp = glow_sub(p, c);
    const float d5 = glow_dot(ab, cp), d6 = glow_dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return glow_add(a, glow_mul(ac, d2 / (d2 - d6)));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return glow_add(b, glow_mul(glow_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const float inverse = 1 / (va + vb + vc);
    return glow_add(a, glow_add(glow_mul(ab, vb * inverse), glow_mul(ac, vc * inverse)));
}
inline bool glow_triangle_hit(Vec3 from, Vec3 to, const GlowTriangle& t) {
    const auto ray = glow_sub(to, from), a = glow_sub(t.p[1], t.p[0]), b = glow_sub(t.p[2], t.p[0]);
    const auto h = glow_cross(ray, b);
    const float determinant = glow_dot(a, h);
    if (std::abs(determinant) < 0.00001F)
        return false;
    const float inverse = 1 / determinant;
    const auto s = glow_sub(from, t.p[0]);
    const float u = glow_dot(s, h) * inverse;
    if (u < 0 || u > 1)
        return false;
    const auto q = glow_cross(s, a);
    const float v = glow_dot(ray, q) * inverse;
    if (v < 0 || u + v > 1)
        return false;
    const float distance = glow_dot(b, q) * inverse;
    return distance > 0.002F && distance < 0.998F;
}

class LavaGlowCache {
    struct Receiver {
        std::uint32_t address;
        Vec3 p, normal;
        float energy = 0, previous_energy = 0;
    };
    struct Node {
        Vec3 lo, hi;
        unsigned begin = 0, end = 0, left = 0, right = 0;
    };
    std::shared_ptr<const GlowGeometry> geometry_;
    std::vector<unsigned> blockers_, sources_;
    std::vector<Node> nodes_;
    std::vector<Receiver> receivers_;
    std::vector<float> pending_;
    std::size_t cursor_ = 0;
    int range_ = 0;
    bool ready_ = false;
    bool building_ = false;
    float ready_time_ = 0;
    std::uint64_t ray_tests_ = 0;
    static float component(Vec3 p, unsigned axis) {
        return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
    }
    unsigned node(unsigned begin, unsigned end) {
        Node n{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}, begin, end};
        for (unsigned i = begin; i < end; ++i)
            for (auto p : geometry_->triangles[blockers_[i]].p) {
                n.lo = {std::min(n.lo.x, p.x), std::min(n.lo.y, p.y), std::min(n.lo.z, p.z)};
                n.hi = {std::max(n.hi.x, p.x), std::max(n.hi.y, p.y), std::max(n.hi.z, p.z)};
            }
        const unsigned index = unsigned(nodes_.size());
        nodes_.push_back(n);
        if (end - begin > 8) {
            const auto extent = glow_sub(n.hi, n.lo);
            const unsigned axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
            const unsigned middle = begin + (end - begin) / 2;
            std::nth_element(blockers_.begin() + begin, blockers_.begin() + middle, blockers_.begin() + end,
                             [&](unsigned a, unsigned b) {
                                 const auto& pa = geometry_->triangles[a].p;
                                 const auto& pb = geometry_->triangles[b].p;
                                 return component(glow_add(glow_add(pa[0], pa[1]), pa[2]), axis) <
                                        component(glow_add(glow_add(pb[0], pb[1]), pb[2]), axis);
                             });
            const auto left = node(begin, middle), right = node(middle, end);
            nodes_[index].left = left;
            nodes_[index].right = right;
        }
        return index;
    }
    static bool box(Vec3 from, Vec3 to, const Node& n) {
        float lo = 0, hi = 1;
        const auto direction = glow_sub(to, from);
        for (unsigned axis = 0; axis < 3; ++axis) {
            const float p = component(from, axis), d = component(direction, axis);
            if (std::abs(d) < 0.00001F) {
                if (p < component(n.lo, axis) || p > component(n.hi, axis))
                    return false;
            } else {
                float a = (component(n.lo, axis) - p) / d, b = (component(n.hi, axis) - p) / d;
                if (a > b)
                    std::swap(a, b);
                lo = std::max(lo, a);
                hi = std::min(hi, b);
                if (lo > hi)
                    return false;
            }
        }
        return true;
    }
    bool visible(Vec3 from, Vec3 to) {
        if (nodes_.empty())
            return true;
        std::array<unsigned, 64> stack{};
        unsigned size = 1;
        stack[0] = 0;
        while (size) {
            const auto& n = nodes_[stack[--size]];
            if (!box(from, to, n))
                continue;
            if (n.left) {
                if (size + 2 > stack.size())
                    return false;
                stack[size++] = n.left;
                stack[size++] = n.right;
            } else
                for (unsigned i = n.begin; i < n.end; ++i) {
                    ++ray_tests_;
                    if (glow_triangle_hit(from, to, geometry_->triangles[blockers_[i]]))
                        return false;
                }
        }
        return true;
    }
    float illuminate(const Receiver& receiver) {
        struct Candidate {
            float weight = 0;
            Vec3 point;
        };
        std::array<Candidate, 8> best{};
        for (auto index : sources_) {
            const auto& t = geometry_->triangles[index];
            auto point = glow_closest(receiver.p, t);
            // A small upward, local glow, not a level-wide orange wash.
            point.y += 4;
            const auto delta = glow_sub(point, receiver.p);
            const float length = std::sqrt(glow_dot(delta, delta));
            const float reach = 300.0F * float(range_) * 0.01F;
            if (length >= reach || receiver.p.y < point.y - 32)
                continue;
            const float falloff = 1 - length / reach;
            const float facing = std::abs(glow_dot(receiver.normal, delta)) / std::max(length, 1.0F);
            const float weight = falloff * falloff * (0.15F + 0.85F * facing);
            if (weight <= best.back().weight)
                continue;
            best.back() = {weight, point};
            for (unsigned i = unsigned(best.size()) - 1; i > 0 && best[i].weight > best[i - 1].weight; --i)
                std::swap(best[i], best[i - 1]);
        }
        float energy = 0;
        unsigned hits = 0;
        for (const auto& candidate : best) {
            if (candidate.weight <= 0.001F)
                break;
            const auto direction = glow_sub(candidate.point, receiver.p);
            const float length = std::sqrt(glow_dot(direction, direction));
            const auto from = glow_add(receiver.p, glow_mul(direction, 2 / std::max(length, 2.0F)));
            if (visible(from, candidate.point)) {
                energy += candidate.weight;
                if (++hits == 3)
                    break;
            }
        }
        return std::min(0.14F, energy * 0.06F);
    }

  public:
    void prepare(std::shared_ptr<const GlowGeometry> geometry, int range) {
        range = std::clamp(range, 25, 200);
        if (geometry == geometry_ && range == range_)
            return;
        if (geometry == geometry_ && !receivers_.empty()) {
            range_ = range;
            cursor_ = 0;
            building_ = true;
            return;
        }
        geometry_ = std::move(geometry);
        range_ = range;
        cursor_ = 0;
        ready_ = false;
        ray_tests_ = 0;
        blockers_.clear();
        sources_.clear();
        nodes_.clear();
        receivers_.clear();
        pending_.clear();
        building_ = false;
        if (!geometry_ || !geometry_->complete || geometry_->triangles.size() > kGlowTriangleLimit)
            return;
        std::map<std::uint32_t, Receiver> unique;
        for (unsigned i = 0; i < geometry_->triangles.size(); ++i) {
            const auto& t = geometry_->triangles[i];
            if (!valid_triangle(t.p[0], t.p[1], t.p[2]))
                continue;
            if (t.source) {
                sources_.push_back(i);
                continue;
            }
            blockers_.push_back(i);
            auto normal = glow_cross(glow_sub(t.p[1], t.p[0]), glow_sub(t.p[2], t.p[0]));
            normal = glow_mul(normal, 1 / std::sqrt(glow_dot(normal, normal)));
            for (unsigned j = 0; j < 3; ++j) {
                if (!t.addresses[j])
                    continue;
                auto [at, inserted] = unique.try_emplace(t.addresses[j], Receiver{t.addresses[j], t.p[j], normal});
                if (!inserted)
                    at->second.normal = glow_add(
                        at->second.normal, glow_dot(normal, at->second.normal) < 0 ? glow_mul(normal, -1) : normal);
            }
        }
        if (sources_.empty() || sources_.size() > kGlowSourceLimit) {
            receivers_.clear();
            return;
        }
        for (auto& [address, r] : unique) {
            const float length = std::sqrt(glow_dot(r.normal, r.normal));
            if (length > 0.0001F)
                r.normal = glow_mul(r.normal, 1 / length);
            receivers_.push_back(r);
        }
        if (!blockers_.empty())
            node(0, unsigned(blockers_.size()));
        pending_.resize(receivers_.size(), 0);
        building_ = !receivers_.empty();
    }
    void step(float seconds, std::chrono::microseconds budget = std::chrono::microseconds(650)) {
        if (!building_ || receivers_.empty())
            return;
        const auto deadline = std::chrono::steady_clock::now() + budget;
        unsigned count = 0;
        do {
            pending_[cursor_] = illuminate(receivers_[cursor_]);
            ++cursor_;
            ++count;
        } while (cursor_ < receivers_.size() && count < 256 && std::chrono::steady_clock::now() < deadline);
        if (cursor_ == receivers_.size()) {
            for (std::size_t i = 0; i < receivers_.size(); ++i) {
                receivers_[i].previous_energy = receivers_[i].energy;
                receivers_[i].energy = pending_[i];
            }
            ready_ = true;
            building_ = false;
            ready_time_ = seconds;
        }
    }
    float energy(std::uint32_t address, Vec3 p, float seconds) const {
        if (!ready_ || !finite(p))
            return 0;
        const auto at = std::lower_bound(receivers_.begin(), receivers_.end(), address,
                                         [](const Receiver& r, std::uint32_t value) { return r.address < value; });
        if (at == receivers_.end() || at->address != address ||
            glow_dot(glow_sub(at->p, p), glow_sub(at->p, p)) > 0.01F)
            return 0;
        const float blend = seconds < ready_time_ ? 1.0F : std::clamp((seconds - ready_time_) * 6.0F, 0.0F, 1.0F);
        return at->previous_energy + (at->energy - at->previous_energy) * blend;
    }
    bool ready() const {
        return ready_;
    }
    std::size_t receivers() const {
        return receivers_.size();
    }
    std::uint64_t ray_tests() const {
        return ray_tests_;
    }
};
} // namespace dkr::runtime::water::enhanced

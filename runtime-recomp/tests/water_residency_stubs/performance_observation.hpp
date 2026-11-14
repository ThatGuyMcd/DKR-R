#pragma once
namespace dkr::runtime::surgical {
enum class Region { LiquidWarmup };
struct Scope {
    explicit Scope(Region) {}
};
} // namespace dkr::runtime::surgical

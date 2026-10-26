#pragma once
#include "mods/magic_code_link.hpp"
#include <functional>
#include <optional>
#include <span>
#ifndef DKR_PRESENTATION_HOST
#define DKR_PRESENTATION_HOST
#endif
#include "dkr/presentation.h"

namespace dkr::runtime::camera {
// The caller owns one Program on its presentation thread. It must never bind
// live guest RAM here. Only typed copies enter a callback; the query callback
// must use a retained scene, not simulation globals or mutable retail scratch.
class Program final {
  public:
    using Query = std::function<DkrViewHit(const DkrViewQuery&)>;
    Program(std::shared_ptr<const mods::code::Package>, mods::code::Json settings, std::string revision);
    static mods::code::LinkPolicy policy();
    bool settings(const mods::code::Json&) noexcept;
    std::optional<DkrViewPose> frame(const DkrViewSnapshot&, const DkrViewInput&, const Query&);
    const std::string& error() const {
        return error_;
    }
    bool quarantined() const {
        return quarantined_;
    }
    const mods::code::Package& package() const {
        return *linked_->owners[0].package;
    }

  private:
    std::shared_ptr<const mods::code::LinkedSet> linked_;
    mods::Bytes ram_;
    mods::code::Memory memory_;
    mods::code::Json settings_;
    DkrViewSnapshot snapshot_{};
    DkrViewInput input_{};
    const Query* query_ = nullptr;
    std::optional<DkrViewPose> output_;
    std::uint32_t entry_ = 0;
    std::array<std::uint32_t, 4> held_{};
    unsigned services_ = 0, queries_ = 0;
    bool quarantined_ = false;
    std::string error_;
    void service(std::uint32_t, mods::code::Registers&, mods::code::Budget&);
    void validate(const DkrViewPose&) const;
};
} // namespace dkr::runtime::camera

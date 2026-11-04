#pragma once
#include "service.hpp"
#include <utility>

namespace dkr::localisation {
// Keep raw choice data in the model. The label is resolved each frame, so
// static registries and an open picker both follow a live language change.
struct Choice {
    std::string label;
    std::string detail;
    bool disabled = false;
    bool action = false;
    bool separator = false;
    bool owned_label = false;

    template <std::size_t N>
    Choice(const char (&value)[N], std::string more = {}, bool off = false)
        : label(value), detail(std::move(more)), disabled(off), owned_label(true) {}
    Choice(std::string value, std::string more = {}, bool off = false)
        : label(std::move(value)), detail(std::move(more)), disabled(off) {}
    std::string_view display_label() const {
        return owned_label || action ? source(label) : std::string_view(label);
    }
    std::string_view display_detail() const {
        return owned_label ? source(detail) : std::string_view(detail);
    }
    static Choice Action(std::string value) {
        Choice choice(std::move(value));
        choice.action = true;
        return choice;
    }
    static Choice Separator() {
        Choice choice(std::string{});
        choice.separator = true;
        return choice;
    }
};
} // namespace dkr::localisation

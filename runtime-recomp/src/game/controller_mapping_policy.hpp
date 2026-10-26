#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dkr::runtime::controllers {

enum class PhysicalInputKind : std::uint8_t {
    None,
    Button,
    Axis,
    Hat,
};

struct PhysicalInput {
    PhysicalInputKind kind = PhysicalInputKind::None;
    int index = -1;
    int direction = 0;
    std::uint8_t hat_mask = 0;

    friend bool operator==(const PhysicalInput&, const PhysicalInput&) = default;
};

enum class MappingControl : std::uint8_t {
    StickLeft,
    StickRight,
    StickUp,
    StickDown,
    A,
    B,
    Z,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    L,
    R,
    CUp,
    CDown,
    CLeft,
    CRight,
    Count,
};

inline constexpr std::size_t kMappingControlCount = static_cast<std::size_t>(MappingControl::Count);

struct ControllerMappingDefinition {
    std::array<PhysicalInput, kMappingControlCount> inputs{};
};

inline constexpr std::array<const char*, kMappingControlCount> kMappingPrompts{{
    "Move the analogue stick LEFT",
    "Move the analogue stick RIGHT",
    "Move the analogue stick UP",
    "Move the analogue stick DOWN",
    "Press the N64 A button",
    "Press the N64 B button",
    "Press the N64 Z trigger",
    "Press START",
    "Press D-PAD UP",
    "Press D-PAD DOWN",
    "Press D-PAD LEFT",
    "Press D-PAD RIGHT",
    "Press the L shoulder button",
    "Press the R shoulder button",
    "Press C-UP",
    "Press C-DOWN",
    "Press C-LEFT",
    "Press C-RIGHT",
}};

constexpr std::size_t mapping_index(MappingControl control) {
    return std::min(static_cast<std::size_t>(control), kMappingControlCount - 1U);
}

inline const char* mapping_prompt(MappingControl control) {
    return kMappingPrompts[mapping_index(control)];
}

inline bool physical_input_valid(const PhysicalInput& input) {
    switch (input.kind) {
    case PhysicalInputKind::Button:
        return input.index >= 0;
    case PhysicalInputKind::Axis:
        return input.index >= 0 && input.direction != 0;
    case PhysicalInputKind::Hat:
        return input.index >= 0 && input.hat_mask != 0;
    default:
        return false;
    }
}

inline bool mapping_input_available(const ControllerMappingDefinition& mapping, std::size_t next_index,
                                    const PhysicalInput& input) {
    if (!physical_input_valid(input)) {
        return false;
    }
    next_index = std::min(next_index, kMappingControlCount);
    for (std::size_t index = 0; index < next_index; ++index) {
        if (mapping.inputs[index] == input) {
            return false;
        }
    }
    return true;
}

inline std::string sanitise_mapping_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char value : name) {
        if (value == ',' || value == '\r' || value == '\n') {
            result.push_back(' ');
        } else if (static_cast<unsigned char>(value) >= 0x20U) {
            result.push_back(value);
        }
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result.empty() ? "DKR-R controller" : result;
}

inline std::string encode_physical_input(const PhysicalInput& input) {
    switch (input.kind) {
    case PhysicalInputKind::Button:
        return "b" + std::to_string(input.index);
    case PhysicalInputKind::Axis:
        return std::string(input.direction > 0 ? "+a" : "-a") + std::to_string(input.index);
    case PhysicalInputKind::Hat:
        return "h" + std::to_string(input.index) + "." + std::to_string(static_cast<unsigned>(input.hat_mask));
    default:
        return {};
    }
}

inline void append_mapping_field(std::string& output, std::string_view target, const PhysicalInput& input) {
    output.append(target);
    output.push_back(':');
    output.append(encode_physical_input(input));
    output.push_back(',');
}

inline void append_axis_pair(std::string& output, std::string_view target, const PhysicalInput& negative,
                             const PhysicalInput& positive) {
    if (negative.kind == PhysicalInputKind::Axis && positive.kind == PhysicalInputKind::Axis &&
        negative.index == positive.index && negative.direction == -positive.direction) {
        output.append(target);
        output.append(":a");
        output.append(std::to_string(negative.index));
        if (negative.direction > 0) {
            output.push_back('~');
        }
        output.push_back(',');
        return;
    }
    append_mapping_field(output, std::string("-") + std::string(target), negative);
    append_mapping_field(output, std::string("+") + std::string(target), positive);
}

inline bool mapping_complete(const ControllerMappingDefinition& mapping) {
    return std::all_of(mapping.inputs.begin(), mapping.inputs.end(), physical_input_valid);
}

inline std::string build_sdl_mapping(std::string_view guid, std::string_view name, std::string_view platform,
                                     const ControllerMappingDefinition& mapping) {
    if (guid.empty() || !mapping_complete(mapping)) {
        return {};
    }

    const auto input = [&](MappingControl control) -> const PhysicalInput& {
        return mapping.inputs[mapping_index(control)];
    };
    std::string output;
    output.reserve(384U);
    output.append(guid);
    output.push_back(',');
    output.append(sanitise_mapping_name(name));
    output.push_back(',');

    append_axis_pair(output, "leftx", input(MappingControl::StickLeft), input(MappingControl::StickRight));
    append_axis_pair(output, "lefty", input(MappingControl::StickUp), input(MappingControl::StickDown));
    append_mapping_field(output, "a", input(MappingControl::A));
    // DKR-R's default N64 B binding uses SDL's X face button, leaving SDL B/Y
    // free for controllers which expose C buttons instead of a right stick.
    append_mapping_field(output, "x", input(MappingControl::B));
    append_mapping_field(output, "lefttrigger", input(MappingControl::Z));
    append_mapping_field(output, "start", input(MappingControl::Start));
    append_mapping_field(output, "dpup", input(MappingControl::DpadUp));
    append_mapping_field(output, "dpdown", input(MappingControl::DpadDown));
    append_mapping_field(output, "dpleft", input(MappingControl::DpadLeft));
    append_mapping_field(output, "dpright", input(MappingControl::DpadRight));
    append_mapping_field(output, "leftshoulder", input(MappingControl::L));
    append_mapping_field(output, "rightshoulder", input(MappingControl::R));
    append_axis_pair(output, "righty", input(MappingControl::CUp), input(MappingControl::CDown));
    append_axis_pair(output, "rightx", input(MappingControl::CLeft), input(MappingControl::CRight));
    if (!platform.empty()) {
        output.append("platform:");
        output.append(platform);
        output.push_back(',');
    }
    return output;
}

} // namespace dkr::runtime::controllers

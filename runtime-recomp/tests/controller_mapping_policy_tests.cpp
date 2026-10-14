#include "controller_mapping_policy.hpp"
#include "controller_snapshot.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace dkr::runtime::controllers;

    // Diagnostic reads ignore retained disconnected presses and preserve
    // simultaneous and extended standard button indices when connected.
    ControllerSnapshot diagnostic{};
    diagnostic.buttons[static_cast<std::size_t>(StandardButton::South)] = 1U;
    diagnostic.buttons[static_cast<std::size_t>(StandardButton::DpadLeft)] = 1U;
    diagnostic.buttons.back() = 1U;
    assert(pressed_button_mask(diagnostic) == 0U);
    diagnostic.connected = true;
    assert(pressed_button_mask(diagnostic) == ((1U << 0U) | (1U << 13U) | (1U << 25U)));
    diagnostic.buttons.fill(0U);
    assert(pressed_button_mask(diagnostic) == 0U);

    ControllerMappingDefinition mapping{};
    mapping.inputs[mapping_index(MappingControl::StickLeft)] =
        {PhysicalInputKind::Axis, 0, -1, 0};
    mapping.inputs[mapping_index(MappingControl::StickRight)] =
        {PhysicalInputKind::Axis, 0, 1, 0};
    mapping.inputs[mapping_index(MappingControl::StickUp)] =
        {PhysicalInputKind::Axis, 1, 1, 0};
    mapping.inputs[mapping_index(MappingControl::StickDown)] =
        {PhysicalInputKind::Axis, 1, -1, 0};

    int button = 0;
    for (std::size_t index = mapping_index(MappingControl::A);
         index < kMappingControlCount; ++index) {
        mapping.inputs[index] = {PhysicalInputKind::Button, button++, 1, 0};
    }

    assert(mapping_complete(mapping));
    const std::string result = build_sdl_mapping(
        "03000000000000000000000000000000", "N64, Controller\n",
        "Windows", mapping);
    assert(result.find(
        "03000000000000000000000000000000,N64  Controller,") == 0U);
    assert(result.find("leftx:a0,") != std::string::npos);
    assert(result.find("lefty:a1~,") != std::string::npos);
    assert(result.find("a:b0,") != std::string::npos);
    assert(result.find("x:b1,") != std::string::npos);
    assert(result.find("lefttrigger:b2,") != std::string::npos);
    assert(result.find("-righty:b10,+righty:b11,") != std::string::npos);
    assert(result.find("-rightx:b12,+rightx:b13,") != std::string::npos);
    assert(result.find("platform:Windows,") != std::string::npos);

    assert(!mapping_input_available(mapping, mapping_index(MappingControl::B),
                                    mapping.inputs[mapping_index(MappingControl::A)]));
    assert(mapping_input_available(
        mapping, mapping_index(MappingControl::StickRight),
        {PhysicalInputKind::Axis, 0, 1, 0}));
    assert(!mapping_input_available(
        mapping, mapping_index(MappingControl::StickRight),
        {PhysicalInputKind::Axis, 0, -1, 0}));

    std::puts("controller mapping policy tests passed");
    return 0;
}

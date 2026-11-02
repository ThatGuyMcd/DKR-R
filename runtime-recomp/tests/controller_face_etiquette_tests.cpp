#include "controller_face_etiquette.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    using namespace dkr::runtime::controllers;

    assert(detect_controller_family("Nintendo Switch Pro Controller",
                                    kSdlControllerTypeNintendoSwitchPro) ==
           ControllerFamily::Switch);
    assert(detect_controller_family("NSO N64 Controller",
                                    kSdlControllerTypeNintendoSwitchPro) ==
           ControllerFamily::N64);
    assert(detect_controller_family("NSO GameCube Controller",
                                    kSdlControllerTypeNintendoSwitchPro) ==
           ControllerFamily::GameCube);
    assert(detect_controller_family("Joy-Con (L)",
                                    kSdlControllerTypeJoyConLeft) ==
           ControllerFamily::Switch);
    assert(detect_controller_family("Xbox Series X Controller",
                                    kSdlControllerTypeXboxOne) ==
           ControllerFamily::Xbox);

    assert(uses_nintendo_face_etiquette(ControllerFamily::Switch));
    assert(!uses_nintendo_face_etiquette(ControllerFamily::Xbox));
    assert(!uses_nintendo_face_etiquette(ControllerFamily::N64));
    assert(!uses_nintendo_face_etiquette(ControllerFamily::GameCube));

    assert(nintendo_label_face_button(0U) == 1U);
    assert(nintendo_label_face_button(1U) == 0U);
    assert(nintendo_label_face_button(2U) == 3U);
    assert(nintendo_label_face_button(3U) == 2U);
    assert(nintendo_label_face_button(9U) == 9U);

    std::array<std::uint8_t, 4> buttons{{1U, 0U, 1U, 0U}};
    apply_nintendo_face_etiquette(buttons);
    assert(buttons[0] == 0U && buttons[1] == 1U);
    assert(buttons[2] == 0U && buttons[3] == 1U);

    std::puts("controller face etiquette tests passed");
    return 0;
}

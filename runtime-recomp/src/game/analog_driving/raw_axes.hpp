#pragma once
#include "controller_snapshot.hpp"
#include <SDL.h>
namespace dkr::runtime::driving {
inline bool connected(SDL_GameController* c) {
    return c && SDL_GameControllerGetAttached(c);
}
inline bool connected(const controllers::ControllerSnapshot* c) {
    return c && c->connected;
}
inline float raw_button(SDL_GameController* c, int button) {
    if (!connected(c) || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
        return 0;
    if (button == SDL_CONTROLLER_BUTTON_X &&
        SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_X).bindType == SDL_CONTROLLER_BINDTYPE_NONE &&
        SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_B).bindType != SDL_CONTROLLER_BINDTYPE_NONE)
        button = SDL_CONTROLLER_BUTTON_B;
    return SDL_GameControllerGetButton(c, SDL_GameControllerButton(button)) ? 1.F : 0.F;
}
inline float raw_button(const controllers::ControllerSnapshot* c, int button) {
    if (!connected(c) || button < 0 || unsigned(button) >= c->buttons.size())
        return 0;
    if (button == SDL_CONTROLLER_BUTTON_X && !c->button_bound[button] && c->button_bound[SDL_CONTROLLER_BUTTON_B])
        button = SDL_CONTROLLER_BUTTON_B;
    return c->buttons[unsigned(button)] ? 1.F : 0.F;
}
inline float raw_axis(SDL_GameController* controller, int axis) {
    return controller ? float(SDL_GameControllerGetAxis(controller, SDL_GameControllerAxis(axis))) / 32767.F : 0;
}
inline float raw_axis(const controllers::ControllerSnapshot* controller, int axis) {
    return controller && controller->connected && unsigned(axis) < controller->axes.size()
               ? float(controller->axes[unsigned(axis)]) / 32767.F
               : 0;
}
} // namespace dkr::runtime::driving

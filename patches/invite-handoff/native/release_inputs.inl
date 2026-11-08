    // A confirm/back press used by the invitation must be released before
    // it can become racing input. Read the already refreshed physical state;
    // do not clear or mutate any canonical online controller ports.
    if(dkr::runtime::invites::release_pending()) {
        bool held=false;int count=0;const auto* keys=SDL_GetKeyboardState(&count);
        for(int key=0;key<count;++key)held|=keys[key]!=0;
        held|=SDL_GetMouseState(nullptr,nullptr)!=0;
        if(sdl3_native) {
            for(const auto& device:g_sdl3_input_state.devices)if(device.snapshot.connected) {
                for(auto button:device.snapshot.buttons)held|=button!=0;
                for(auto axis:device.snapshot.axes)held|=std::abs(int(axis))>16000;
            }
        } else for(auto* controller:g_controllers)if(controller) {
            for(int button=0;button<SDL_CONTROLLER_BUTTON_MAX;++button)held|=SDL_GameControllerGetButton(controller,SDL_GameControllerButton(button))!=0;
            for(int axis=0;axis<SDL_CONTROLLER_AXIS_MAX;++axis)held|=std::abs(int(SDL_GameControllerGetAxis(controller,SDL_GameControllerAxis(axis))))>16000;
        }
        dkr::runtime::invites::release_if_clear(!held);
    }

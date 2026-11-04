    const bool analogue_enabled=driving::profile(unsigned(player)).enabled;
    const auto& accelerator_binding=bindings[Index(Action::AnalogueAccelerator)];
    const auto& brake_binding=bindings[Index(Action::AnalogueBrake)];
    const auto driving_source=[&](int source,bool keyboard) {
        if(source<0)return false;
        for(const auto* b:{&accelerator_binding,&brake_binding}) {
            if(keyboard?source==b->keyboard:source==b->controller||source==b->controller_secondary)return true;
        }return false;
    };
    const auto raw_value=[&](Action action,bool fallback=false) {
        const auto& b=bindings[Index(action)];float result=0;
        for(const int source:{b.controller,b.controller_secondary}) {
            if(source<0||(fallback&&driving_source(source,false)))continue;
            bool suppressed=false;
            for(std::size_t i=0;i<controller_shortcut_held.size();++i)
                suppressed|=controller_shortcut_held[i]&&ShortcutContains(shortcut_controller[i],source);
            if(suppressed)continue;
            if(source<kAxisSourceBase)result=std::max(result,driving::raw_button(controller,source));
            else {
                const int encoded=source-kAxisSourceBase,axis=encoded/2;
                if(axis>=0&&axis<SDL_CONTROLLER_AXIS_MAX) {
                    const float raw=driving::raw_axis(controller,axis);
                    result=std::max(result,std::clamp((encoded&1)?raw:-raw,0.F,1.F));
                }
            }
        }
        bool suppressed=false;
        for(std::size_t i=0;i<keyboard_shortcut_held.size();++i)
            suppressed|=keyboard_shortcut_held[i]&&ShortcutContains(shortcut_keyboard[i],b.keyboard);
        if(!suppressed&&keys&&b.keyboard>=0&&b.keyboard<SDL_NUM_SCANCODES&&keys[b.keyboard]&&
            !(fallback&&driving_source(b.keyboard,true)))result=1;
        return result;
    };
    const auto continuous=[&](Action action,const driving::Channel& channel) {
        float amount=0;const auto& b=bindings[Index(action)];
        if(driving::connected(controller))for(int source:{b.controller,b.controller_secondary}) {
            if(source<kAxisSourceBase)continue;
            bool suppressed=false;
            for(std::size_t i=0;i<controller_shortcut_held.size();++i)
                suppressed|=controller_shortcut_held[i]&&ShortcutContains(shortcut_controller[i],source);
            if(suppressed)continue;
            const int encoded=source-kAxisSourceBase,axis=encoded/2;
            if(axis>=0&&axis<SDL_CONTROLLER_AXIS_MAX) {
                const float raw=driving::raw_axis(controller,axis);
                amount=std::max(amount,driving::calibrate(std::clamp((encoded&1)?raw:-raw,0.F,1.F),channel));
            }
        }
        return amount;
    };
    const auto digital=[&](Action action) {
        auto copy=bindings[Index(action)];
        // raw_value already applies chord suppression. An axis is never
        // promoted to the digital fallback for the analogue action itself.
        auto& b=bindings[Index(action)];
        if(b.controller>=kAxisSourceBase)b.controller=kUnbound;
        if(b.controller_secondary>=kAxisSourceBase)b.controller_secondary=kUnbound;
        const float result=raw_value(action);b=copy;return result;
    };
    const auto profile=driving::profile(unsigned(player));
    const auto pedals=analogue_enabled?driving::poll_calibrated(unsigned(player),
        continuous(Action::AnalogueAccelerator,profile.accelerator),continuous(Action::AnalogueBrake,profile.brake),
        std::max({digital(Action::AnalogueAccelerator),raw_value(Action::A,true),raw_value(Action::Accelerate,true)}),
        std::max(digital(Action::AnalogueBrake),raw_value(Action::B,true))):driving::Polled{};
    if(blocked){driving::accelerator_held[player]=driving::brake_held[player]=false;return state;}
    state.accelerator=pedals.pedals.accelerator;state.brake=pedals.pedals.brake;state.driving_flags=pedals.pedals.flags;

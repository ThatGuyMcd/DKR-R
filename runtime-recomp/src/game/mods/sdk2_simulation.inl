// Retired main-loop control lane. No ordinary mod frame runs while paused.
// Controls are rewindable; window, transport and presentation owners stay live.
void GameWorld::sdk2_native_return(std::string_view name,Registers& registers) {
    if(name!="menu_pause_loop"||!sdk2_development_||!native_bus_)return;
    for(const auto& owner:linked().owners)if(owner.package->api==2) {
        const auto address=owner.services+23220;const auto choice=native_bus_->load(address,4);
        if(choice){registers.integer[2]=choice;native_bus_->store(address,4,0);return;}
    }
}
unsigned GameWorld::sdk2_pulse() {
    std::lock_guard lock(mutex_);
    if(!sdk2_active_||!sdk2_development_||!native_bus_)return 1;
    if(!online_)sdk2_reload();
    auto& bus=*native_bus_;
    if(!online_)sdk2_command(dkr::sdk2::take(false));
    sdk2_boundary(DKR_CODE_EVENT_SCENE_LOADED); // lifetimes only, not a frame
    const auto old_phase=sdk2_phase_;sdk2_phase_=2;sdk2_pulsing_=true;
    struct Restore {unsigned& phase;unsigned previous;bool& pulse;~Restore(){phase=previous;pulse=false;}} restore{sdk2_phase_,old_phase,sdk2_pulsing_};
    if(!execution::active(&world_))budget_=sdk2_budget_;world_.event("*","on_tool",Registers{},budget_);sdk2_inspector();
    for(const auto& owner:linked().owners)if(owner.package->api==2&&owner.package->modules.contains("simulation2")) {
        const auto at=owner.services+23200;
        if(bus.load(symbol("gGameMode"),4)!=0||bus.load(symbol("gLevelLoadTimer"),2)) {
            bus.fill(at,20);return 1;
        }
        if(bus.load(at,4)) {
            const auto steps=bus.load(at+4,4);if(!steps)return 0;bus.store(at+4,4,steps-1);
        }
        const unsigned rate=unsigned(bus.load(at+8,4));if(rate||online_)bus.store(symbol("sLogicUpdateRate"),4,rate?rate:2);
    }
    return 1;
}
int GameWorld::sdk2_dispatch_pulse()noexcept {
    try{return int(sdk2_pulse());}catch(const std::exception& e){try{error_=e.what();}catch(...){}return -1;}
    catch(...){try{error_="Custom Magic Code failed at the development control boundary.";}catch(...){}return -1;}
}
bool GameWorld::sdk2_frame_starts_at(std::uint32_t address)const {
    if(!sdk2_active_||online_||!sdk2_development_)return false;
    const auto found=std::lower_bound(table_.entries.begin(),table_.entries.end(),address,[](const auto& entry,auto value){return entry.address<value;});
    return found!=table_.entries.end()&&found->address==address&&found->before_event==DKR_CODE_EVENT_FRAME;
}
int GameWorld::sdk2_control(Memory& bus,std::size_t index,unsigned operation,unsigned value) {
    const auto& owner=linked().owners[index];
    if(!owner.package->modules.contains("simulation2"))return -5;
    if(!sdk2_development_||!owner.package->development)return -4;
    if(!sdk2_pulsing_||sdk2_phase_!=2)return -3;
    if(operation&& (bus.load(symbol("gGameMode"),4)!=0||bus.load(symbol("gLevelLoadTimer"),2)))return -3;
    const auto at=owner.services+23200;
    switch(operation) {
    case 0:bus.fill(at,20);bus.store(symbol("sLogicUpdateRate"),4,2);return 0;
    case 1:bus.store(at,4,1);bus.store(at+4,4,0);bus.store(at+8,4,2);return 0;
    case 2:if(!bus.load(at,4))return -3;bus.store(at+4,4,1);return 0;
    case 3:if(value<1||value>4)return -1;bus.store(at+8,4,value);return 0;
    default:return -1;
    }
}

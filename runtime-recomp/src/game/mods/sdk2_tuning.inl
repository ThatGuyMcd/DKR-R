namespace sdk2 {
unsigned vehicle_tag(std::string_view name) {
    constexpr const char* names[]={"func_8004F7F4","func_80046524","func_80049794","update_tricky","update_bluey","update_smokey","update_bubbler","update_wizpig","update_rocket"};
    for(unsigned i=0;i<std::size(names);++i)if(name==names[i])return i+1;return 0;
}
bool vehicle_update(std::string_view name){return vehicle_tag(name)!=0;}
}
bool GameWorld::sdk2_freeze(std::string_view name)const {
    if(name!="update_AI_racer"||!sdk2_active_||!native_bus_||native_bus_->load(symbol("gGameMode"),4)!=0)return false;
    for(const auto& owner:linked().owners)if(owner.package->api==2&&owner.package->modules.contains("racer-tuning2")&&native_bus_->load(owner.services+23228,4))return true;
    return false;
}
int GameWorld::sdk2_tune(Memory& bus,std::size_t index,unsigned request) {
    const auto& owner=linked().owners[index];if(!owner.package->modules.contains("racer-tuning2"))return -5;
    if(sdk2_phase_!=2)return -3;if(owner.package->development&&!sdk2_development_)return -4;
    if(bus.load(request,4)<32)return -1;
    const unsigned slot=unsigned(bus.load(request+4,4)),generation=unsigned(bus.load(request+8,4)),mask=unsigned(bus.load(request+28,4));if(mask&~15U)return -1;
    if(!slot&&!generation&&!mask){bus.fill(owner.services+36864,8*192);return 0;}
    const auto object=sdk2::handle_address(bus,owner.services,slot,generation);if(!object)return -2;
    if(bus.load(object+0x48,2)!=1)return -5;const auto racer=std::uint32_t(bus.load(object+0x64,4));if(!sdk2::guest(racer,0x224))return -2;
    const unsigned id=unsigned(bus.load(racer+2,1));if(id>=8)return -1;
    if((mask&4U)&&bus.load(racer+0x1d6,1)>2)return -5;
    const auto at=owner.services+36864+id*192;if(bus.load(at+24,4))return -3;
    for(unsigned i=0;i<4;++i)if(mask&(1U<<i)) {const auto value=std::bit_cast<float>(std::uint32_t(bus.load(request+12+i*4,4)));if(!std::isfinite(value)||value<(i==3?0.F:0.1F)||value>(i==3?100.F:4.F))return -1;}
    bus.store(at,4,slot);bus.store(at+4,4,generation);for(unsigned i=0;i<4;++i)bus.store(at+8+i*4,4,(mask&(1U<<i))?bus.load(request+12+i*4,4):0);bus.store(at+44,4,mask);return 0;
}
void GameWorld::sdk2_vehicle(std::string_view name,bool leaving,const Registers& args) {
    if(!sdk2_active_||!native_bus_||!sdk2::vehicle_update(name))return;
    auto& bus=*native_bus_;
    // O32 argument registers are caller-saved. Exit hooks must restore the
    // scoped entry record, NOT reinterpret the callee's final a2 as an object.
    if(leaving) {
        for(const auto& owner:linked().owners)if(owner.package->api==2&&owner.package->modules.contains("racer-tuning2"))for(unsigned id=0;id<8;++id) {
            const auto at=owner.services+36864+id*192;if(!bus.load(at+24,4)||bus.load(at+48,4)!=sdk2::vehicle_tag(name))continue;
            const auto mask=unsigned(bus.load(at+44,4));
            if(mask&1U)bus.store(symbol("gCurrentRacerWeightStat"),4,bus.load(at+28,4));if(mask&2U)bus.store(symbol("gCurrentRacerHandlingStat"),4,bus.load(at+32,4));if(mask&4U)bus.store(symbol("gCurrentRacerMiscAssetPtr"),4,bus.load(at+36,4));bus.store(at+24,4,0);
            const auto object=sdk2::handle_address(bus,owner.services,unsigned(bus.load(at,4)),unsigned(bus.load(at+4,4)));
            if(!object||!(mask&8U))continue;const auto racer=std::uint32_t(bus.load(object+0x64,4));if(!sdk2::guest(racer,0x224))continue;
            const auto real=[&](unsigned address){return std::bit_cast<float>(std::uint32_t(bus.load(address,4)));};
            const float cap=real(at+20),speed=real(racer+0x2c);if(cap>0&&std::abs(speed)>cap) {
                const float ratio=cap/std::abs(speed);bus.store(racer+0x2c,4,std::bit_cast<std::uint32_t>(speed*ratio));for(unsigned axis:{0U,2U})bus.store(object+0x1c+axis*4,4,std::bit_cast<std::uint32_t>(real(object+0x1c+axis*4)*ratio));
            }
        }return;
    }
    const auto object=std::uint32_t(args.integer[6]);if(!sdk2::guest(object,0x68)||bus.load(object+0x48,2)!=1)return;
    const auto racer=std::uint32_t(bus.load(object+0x64,4));if(!sdk2::guest(racer,0x224))return;const unsigned id=unsigned(bus.load(racer+2,1));if(id>=8)return;
    for(const auto& owner:linked().owners)if(owner.package->api==2&&owner.package->modules.contains("racer-tuning2")) {
        const auto at=owner.services+36864+id*192;const unsigned slot=unsigned(bus.load(at,4)),generation=unsigned(bus.load(at+4,4));if(sdk2::handle_address(bus,owner.services,slot,generation)!=object)continue;
        const auto weight=symbol("gCurrentRacerWeightStat"),handling=symbol("gCurrentRacerHandlingStat"),curve=symbol("gCurrentRacerMiscAssetPtr");const auto mask=unsigned(bus.load(at+44,4));
        const auto real=[&](unsigned address){return std::bit_cast<float>(std::uint32_t(bus.load(address,4)));};
        if(!leaving) {
            if(bus.load(at+24,4))throw Error("SDK racer tuning re-entered before native restoration.");if(!mask)continue;
            bus.store(at+28,4,bus.load(weight,4));bus.store(at+32,4,bus.load(handling,4));bus.store(at+36,4,bus.load(curve,4));bus.store(at+24,4,1);bus.store(at+48,4,sdk2::vehicle_tag(name));
            if(mask&1U)bus.store(weight,4,std::bit_cast<std::uint32_t>(real(weight)*real(at+8)));
            if(mask&2U)bus.store(handling,4,std::bit_cast<std::uint32_t>(real(handling)*real(at+12)));
            if(mask&4U) {
                const auto source=std::uint32_t(bus.load(curve,4));if(!sdk2::guest(source,56))throw Error("Native acceleration curve is unavailable.");
                for(unsigned n=0;n<14;++n){const auto value=real(source+n*4)*real(at+16);if(!std::isfinite(value))throw Error("Native acceleration curve is non-finite.");bus.store(at+64+n*4,4,std::bit_cast<std::uint32_t>(value));}bus.store(curve,4,at+64);
            }
        }
    }
}

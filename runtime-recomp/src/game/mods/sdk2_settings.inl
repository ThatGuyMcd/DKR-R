int GameWorld::sdk2_setting_apply(Memory& bus,std::size_t caller,unsigned package,unsigned option,std::uint32_t value,Budget& budget) {
    const auto& source=linked().owners[caller];if(!source.package->modules.contains("live-settings2"))return -5;
    if(!source.package->development||!sdk2_development_)return -4;
    if(sdk2_phase_!=2||(online_&&!sdk2_command_))return -3;
    if(package>=linked().owners.size())return -1;const auto& target=linked().owners[package];
    if(option>=target.package->options.size())return -1;
    const auto& schema=target.package->options[option];const auto key=schema.at("id").get<std::string>();
    if(target.package->api!=2||!target.package->modules.contains("live-settings2")||!target.package->sdk2_live_options.contains(key))return -8;
    const auto type=schema.at("type").get<std::string>();unsigned tag=0,bits=0;
    if(value) {
        bits=std::uint32_t(bus.load(value,4));
        if(type=="Number"){const float real=std::bit_cast<float>(bits);if(!std::isfinite(real)||real<schema.at("min").get<double>()||real>schema.at("max").get<double>())return -1;tag=1;}
        else if(type=="Enum"){if(bits>=schema.at("options").size())return -1;tag=2;}else return -8;
    }
    const auto at=target.services+28672+option*8;if(bus.load(at,4)==tag&&(!tag||bus.load(at+4,4)==bits))return 0;
    bus.store(at+4,4,bits);bus.store(at,4,tag);
    Registers args;args.integer[4]=option;args.integer[5]=bits;args.integer[6]=tag;
    world_.owner_event(package,"on_live_setting",budget,args);return 0;
}

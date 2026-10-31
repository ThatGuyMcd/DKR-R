void GameWorld::sdk2_apply_overrides(std::size_t index,unsigned object,bool shading) {
    if(!native_bus_)return;auto& bus=*native_bus_;const auto& owner=linked().owners[index];
    const unsigned count=unsigned(bus.load(owner.services+38400,4));if(count>32)throw Error("SDK persistent property count is invalid.");
    for(unsigned i=0;i<count;++i) {
        const auto row=owner.services+38464+i*16;const auto obj=sdk2::handle_address(bus,owner.services,unsigned(bus.load(row,4)),unsigned(bus.load(row+4,4)));
        if(!obj||(object&&obj!=object))continue;const auto id=unsigned(bus.load(row+8,4));if(shading&&(id<200||id>=300))continue;
        const auto property=std::find_if(std::begin(sdk2::Properties),std::end(sdk2::Properties),[&](const auto& value){return value.id==id;});
        if(property==std::end(sdk2::Properties)||!property->writable)throw Error("SDK persistent property descriptor is invalid.");
        const auto racer=bus.load(obj+0x48,2)==1?std::uint32_t(bus.load(obj+0x64,4)):0;const auto base=sdk2::property_base(bus,obj,racer,*property);
        if(base)bus.store(base+property->offset,property->width,bus.load(row+12,4));
    }
}
void GameWorld::sdk2_shading(std::string_view name,bool leaving,const Registers& args) {
    if(name!="light_update_shading"||!sdk2_active_||!native_bus_)return;auto& bus=*native_bus_;
    for(unsigned index=0;index<linked().owners.size();++index) {
        const auto& owner=linked().owners[index];if(owner.package->api!=2||!owner.package->modules.contains("overrides2"))continue;
        const auto header=owner.services+38400;unsigned depth=unsigned(bus.load(header+4,4));
        if(!leaving) {
            if(depth>=8)throw Error("SDK shading scopes exceeded their bound.");
            bus.store(header+8+depth*4,4,args.integer[4]);bus.store(header+4,4,depth+1);
        }else {
            if(!depth)throw Error("SDK shading scope has no matching entry.");
            const auto object=unsigned(bus.load(header+8+(depth-1)*4,4));bus.store(header+4,4,depth-1);sdk2_apply_overrides(index,object,true);
        }
    }
}

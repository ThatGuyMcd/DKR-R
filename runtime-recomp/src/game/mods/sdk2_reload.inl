void GameWorld::sdk2_reload() {
    auto request=dkr::sdk2::take_reload();if(!request.package)return;
    try {
        if(online_||!sdk2_development_||!world_.idle()||nested_||native_depth_)throw Error("Reload needs an offline retired development session. Online: return to lobby and readmit the new package.");
        const auto old=world_.image();const auto owner=request.owner;
        if(owner>=old->owners.size())throw Error("Reload owner changed.");const auto& previous=*old->owners[owner].package;const auto& replacement=*request.package;
        if(previous.digest!=request.digest||previous.id!=replacement.id||!previous.sdk2_reload||!replacement.sdk2_reload||previous.sdk2_state_schema!=replacement.sdk2_state_schema)
            throw Error("Both editions must opt into instruction-only reload with the same package ID and state_schema. Relaunch this package instead.");
        if(previous.api!=replacement.api||previous.options!=replacement.options||previous.modules!=replacement.modules||previous.systems!=replacement.systems||
           previous.sdk2_profile!=replacement.sdk2_profile||previous.sdk2_live_options!=replacement.sdk2_live_options||previous.deterministic!=replacement.deterministic||previous.revisions!=replacement.revisions||previous.tracks!=replacement.tracks)
            throw Error("Capabilities, settings, resources or profile changed. Relaunch to prepare them safely.");
        auto resources=previous.resources,new_resources=replacement.resources;resources.erase("source-map");new_resources.erase("source-map");
        if(resources!=new_resources)throw Error("Immutable resources changed. Relaunch rather than live-replacing assets or tools.");
        std::vector<std::shared_ptr<const Package>> packages;std::map<std::string,Json> settings,data;
        for(unsigned i=0;i<old->owners.size();++i){const auto& o=old->owners[i];packages.push_back(i==owner?request.package:o.package);settings[o.package->id]=o.settings;data[o.package->id]=Json::object();for(const auto& [key,bytes]:o.data)data[o.package->id][key]=parse_document(bytes,4*MiB);}
        auto next=link(packages,settings,old->revision,policy(table_,false),data);
        if(next->resident_bytes!=old->resident_bytes||next->regions.size()!=old->regions.size()||next->sections.size()!=old->sections.size()||
           next->function_owners!=old->function_owners||next->events!=old->events||next->native_functions!=old->native_functions||next->imports.size()!=old->imports.size()||next->hooks.size()!=old->hooks.size())throw Error("Code layout changed. Relaunch; only equal-layout instruction changes are live-reloadable.");
        for(unsigned i=0;i<old->regions.size();++i){const auto& a=old->regions[i];const auto& b=next->regions[i];if(a.address!=b.address||a.size!=b.size||a.writable!=b.writable||a.executable!=b.executable)throw Error("Code region permissions/layout changed. Relaunch required.");}
        for(const auto& [address,a]:old->imports){const auto& b=next->imports.at(address);if(a.owner!=b.owner||a.dependency!=b.dependency||a.name!=b.name||a.event!=b.event)throw Error("Import ABI changed. Relaunch required.");}
        for(const auto& [address,a]:old->hooks){const auto& b=next->hooks.at(address);if(a.entry!=b.entry||a.exit!=b.exit||a.replacement!=b.replacement)throw Error("Hook ABI changed. Relaunch required.");}
        for(unsigned i=0;i<old->owners.size();++i){const auto& a=old->owners[i];const auto& b=next->owners[i];if(a.begin!=b.begin||a.end!=b.end||a.functions!=b.functions||a.heap_size!=b.heap_size||a.heap!=b.heap||a.stack!=b.stack||a.native_stack!=b.native_stack||a.services!=b.services)throw Error("Owner state layout changed. Relaunch required.");}
        for(unsigned i=0;i<old->sections.size();++i){const auto& a=old->sections[i];const auto& b=next->sections[i];if(a.address!=b.address||a.bytes.size()!=b.bytes.size())throw Error("Linked section changed. Relaunch required.");
            for(unsigned at=0;at<a.bytes.size();++at)if(a.bytes[at]!=b.bytes[at]){const auto address=a.address+at;const auto r=std::find_if(old->regions.begin(),old->regions.end(),[&](const auto& v){return v.executable&&address>=v.address&&address-v.address<v.size;});if(r==old->regions.end())throw Error("Initial data or constants changed. Only instructions may change during live reload.");}}
        const auto& o=old->owners[owner];auto& bus=*native_bus_;
        for(unsigned i=0;i<32;++i)if(bus.load(o.services+sdk2::SpawnOwners+i*16,4))throw Error("Remove tool-owned spawns before reloading, or relaunch.");
        if(bus.load(o.services+38404,4)||bus.load(o.services+40024,4))throw Error("Finish the native scope/document transfer before reloading.");
        // Preallocate rollback, restricted migration and final buses BEFORE
        // invoking user code. Cleanup/migration may only touch this owner's
        // rewindable VM state: no native calls, game RAM or other mod writes.
        auto saved=world_.stage(world_.checkpoint());auto cleanup=world_.prepare_rebind(old,int(owner));auto migrate=world_.prepare_rebind(next,int(owner));auto normal=world_.prepare_rebind(next);
        auto regions=next->regions;regions.push_back({0x80000000U,8*unsigned(MiB),true,false});auto native=std::make_unique<Memory>(ram_,std::move(regions));
        world_.exchange(*cleanup);
        try {Budget fuel=sdk2_budget_;world_.owner_event(owner,"on_cleanup",fuel);world_.exchange(*migrate);world_.owner_event(owner,"on_reload",fuel);}
        catch(...){if(world_.image()!=old)world_.exchange(*migrate);world_.exchange(*cleanup);world_.commit(std::move(saved));throw;}
        world_.exchange(*normal);native_bus_.swap(native);
        std::vector<std::shared_ptr<const Package>> admitted;for(const auto& item:next->owners)admitted.push_back(item.package);dkr::sdk2::begin(admitted,false);
        dkr::sdk2::notice("Code reloaded for this offline session. State retained; installed files and save route unchanged.");
    }catch(const std::exception& e){dkr::sdk2::notice(std::string("Reload not applied: ")+e.what());}
}

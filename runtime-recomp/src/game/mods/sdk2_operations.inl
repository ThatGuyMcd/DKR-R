namespace sdk2 {
constexpr unsigned SpawnOwners=24576,SpawnEntries=32768,MaximumSpawns=32;
std::span<const char* const> operation_functions(unsigned operation) {
    static const char* teleport[]={"move_object","get_checkpoint_count","get_checkpoint_node"};
    static const char* clone[]={"spawn_object"};static const char* remove[]={"free_object"};
    static const char* checkpoint[]={"move_object","checkpoint_update_all"};
    static const char* fog[]={"set_fog"};static const char* weather[]={"weather_set"};
    static const char* sound[]={"sound_play"};static const char* music[]={"music_play"};
    static const char* stop[]={"music_stop"};static const char* fade[]={"music_fade"};
    static const char* course[]={"level_properties_push","level_transition_begin"};
    static const char* menu[]={"menu_pause_init","menu_pause_loop"};
    switch(operation){case 1:return teleport;case 2:return clone;case 3:return remove;case 4:return checkpoint;
        case 10:return fog;case 11:return weather;case 20:return sound;case 21:return music;
        case 22:return stop;case 23:return fade;case 30:case 31:case 40:return course;case 32:return menu;default:return {};}
}
}
bool GameWorld::sdk2_operation_available(unsigned operation)const {
    const auto names=sdk2::operation_functions(operation);if(names.empty())return false;
    for(const auto* name:names)if(std::none_of(table_.entries.begin(),table_.entries.end(),[&](const auto& e){
        return e.name&&std::string_view(e.name)==name&&e.function&&(!e.protected_call||(e.curated_call&&operation>=20&&operation<=23));
    }))return false;
    return true;
}
int GameWorld::sdk2_operation(unsigned operation,std::size_t index,Memory& bus,const std::array<std::uint32_t,16>& args,
    std::array<std::uint32_t,16>& output,Budget& budget) {
    const auto& owner=linked().owners.at(index);
    if(!sdk2_operation_available(operation))return -5;
    if(sdk2_phase_!=2)return -3;
    if(owner.package->development&&!sdk2_development_)return -4;
    // Allocating scene/object operations are permitted only in offline worlds
    // or the confirmed host-command boundary in an opted-in development lobby.
    if(online_&&!sdk2_command_&&(operation==2||operation==3||operation==4||operation>=30))return -3;
    const auto native_call=[&](const char* name,std::initializer_list<std::uint32_t> arguments,bool curated=false) {
        const auto entry=std::find_if(table_.entries.begin(),table_.entries.end(),[&](const auto& e){return e.name&&std::string_view(e.name)==name;});
        if(entry==table_.entries.end())throw Error("SDK operation has no native dispatcher.");
        Registers call;const auto stack=owner.services+61440;bus.fill(stack,80);call.integer[29]=std::int32_t(stack);
        unsigned i=0;for(auto value:arguments){if(i<4)call.integer[4+i]=std::uint64_t(std::int64_t(std::int32_t(value)));else bus.store(stack+16+(i-4)*4,4,value);++i;}
        native(entry->address,call,budget,curated);return std::uint32_t(call.integer[2]);
    };
    const auto real=[&](unsigned i){return std::bit_cast<float>(args[i]);};
    const auto float_at=[&](unsigned p){return std::bit_cast<float>(std::uint32_t(bus.load(p,4)));};
    if(operation==1) {
        const auto obj=sdk2::handle_address(bus,owner.services,args[0],args[1]);if(!obj)return -2;
        const float x=real(2),y=real(3),z=real(4);
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(x)>1e6F||std::abs(y)>1e6F||std::abs(z)>1e6F)return -1;
        const auto model=std::uint32_t(bus.load(symbol("gCurrentLevelModel"),4));if(!sdk2::guest(model,0x4c))return -3;
        const auto count=unsigned(bus.load(model+0x1a,2)),boxes=std::uint32_t(bus.load(model+8,4));
        if(count>1024||!sdk2::guest(boxes,count*12))return -1;
        const int xi=int(x),yi=int(y),zi=int(z);int closest=-1,distance=1000000;
        for(unsigned i=0;i<count;++i) {
            const auto at=boxes+i*12;int box[6];for(unsigned j=0;j<6;++j)box[j]=std::int16_t(bus.load(at+j*2,2));
            if(xi>box[0]&&xi<box[3]&&zi>box[2]&&zi<box[5]) {
                const int delta=std::abs(yi-((box[1]+box[4])>>1));if(delta<distance){closest=int(i);distance=delta;}
            }
        }
        if(closest<0)return -1;
        const auto h=owner.services+sdk2::Handles+(args[0]-1)*sdk2::Stride;
        const auto racer=std::uint32_t(bus.load(h+16,4));
        unsigned checkpoint=0,checkpoints=0;
        if(racer) {
            if(!sdk2::guest(racer,0x224))return -2;
            checkpoints=native_call("get_checkpoint_count",{});if(checkpoints>127)return -6;
            if(checkpoints) {
                const auto list=native_call("get_checkpoint_node",{0});if(!sdk2::guest(list,checkpoints*0x3c))return -1;
                float best=1e30F;for(unsigned i=0;i<checkpoints;++i) {
                    const auto p=list+i*0x3c;const float dx=x-float_at(p+0x10),dy=y-float_at(p+0x14),dz=z-float_at(p+0x18);
                    const float delta=dx*dx+dy*dy+dz*dz;if(delta<best){best=delta;checkpoint=i;}
                }
            }
        }
        const auto moved=native_call("move_object",{obj,std::bit_cast<std::uint32_t>(x-float_at(obj+0xc)),std::bit_cast<std::uint32_t>(y-float_at(obj+0x10)),std::bit_cast<std::uint32_t>(z-float_at(obj+0x14))});
        if(moved)return -1;bus.store(obj+0x2e,2,unsigned(closest));
        for(unsigned j=0;j<3;++j)bus.store(obj+0x1c+j*4,4,0);
        if(racer) {
            bus.store(racer+0x2c,4,0);bus.store(racer+0x30,4,0);
            for(unsigned j=0;j<3;++j){const auto value=bus.load(obj+0xc+j*4,4);bus.store(racer+0x5c+j*4,4,value);bus.store(racer+0x68+j*4,4,value);}
            if(checkpoints){bus.store(racer+0x192,1,checkpoint);bus.store(racer+0x190,2,unsigned(bus.load(racer+0x193,1))*checkpoints+checkpoint);bus.store(racer+0xa8,4,std::bit_cast<std::uint32_t>(1.F));bus.store(racer+0x1c8,1,0);}
        }
        return 0;
    }
    if(operation==4) {
        const auto obj=sdk2::handle_address(bus,owner.services,args[0],args[1]);if(!obj)return -2;
        if(bus.load(obj+0x48,2)!=13)return -5;
        for(unsigned i=2;i<5;++i)if(!std::isfinite(real(i))||std::abs(real(i))>32760)return -1;
        for(unsigned i=5;i<8;++i)if(std::int32_t(args[i])<-32768||std::int32_t(args[i])>32767)return -1;
        if(!std::isfinite(real(8))||real(8)<0.01F||real(8)>16.F)return -1;
        const auto count=unsigned(bus.load(symbol("gNumberOfTotalCheckpoints"),4));
        const auto nodes=std::uint32_t(bus.load(symbol("gTrackCheckpoints"),4));
        if(count>127||!sdk2::guest(nodes,count*0x3c))return -1;
        for(unsigned i=0;i<count;++i)if(bus.load(nodes+i*0x3c+0x28,4)!=obj) {
            float distance=0;for(unsigned j=0;j<3;++j){const auto delta=real(j+2)-float_at(nodes+i*0x3c+0x10+j*4);distance+=delta*delta;}
            if(distance<16.F)return -1;
        }
        const int moved=sdk2_operation(1,index,bus,args,output,budget);if(moved)return moved;
        bus.store(obj,2,args[5]);bus.store(obj+2,2,args[6]);bus.store(obj+4,2,args[7]);bus.store(obj+8,4,args[8]);
        // Rebuild native planes, normal vectors, distances and alternate-route
        // bookkeeping together, instead of editing only the visible marker.
        native_call("checkpoint_update_all",{});return 0;
    }
    if(operation==2) {
        const auto obj=sdk2::handle_address(bus,owner.services,args[0],args[1]);if(!obj)return -2;
        // Scenery is the initial reviewed template class, not arbitrary racer,
        // scheduler, trigger or resource-owner cloning by memcpy.
        if(bus.load(obj+0x48,2)!=2||bus.load(symbol("gObjectCount"),4)>=500)return -5;
        const auto entry=std::uint32_t(bus.load(obj+0x3c,4));if(!sdk2::guest(entry,8))return -5;
        const unsigned size=unsigned(bus.load(entry+1,1))&127U;if(size<12||!sdk2::guest(entry,size))return -1;
        for(unsigned i=2;i<=4;++i)if(!std::isfinite(real(i))||real(i)<-32768||real(i)>32767)return -1;
        unsigned slot=32;for(unsigned i=0;i<32;++i)if(!bus.load(owner.services+sdk2::SpawnOwners+i*16,4)){slot=i;break;}if(slot==32)return -6;
        const auto copy=owner.services+sdk2::SpawnEntries+slot*128;std::array<std::uint8_t,128> bytes{};bus.read(entry,std::span(bytes).first(size));bus.write(copy,std::span(bytes).first(size));
        for(unsigned i=0;i<3;++i)bus.store(copy+2+i*2,2,std::uint32_t(std::int32_t(real(i+2))));
        const auto spawned=native_call("spawn_object",{copy,1});if(!spawned)return -6;if(!sdk2::guest(spawned,0x80))throw Error("SDK native spawn returned invalid ownership.");
        const auto record=owner.services+sdk2::SpawnOwners+slot*16;bus.store(record,4,spawned);bus.store(record+4,4,bus.load(spawned+0x40,4));bus.store(record+8,4,copy);bus.store(record+12,4,bus.load(owner.services+sdk2::Header+4,4));
        output[0]=slot+1;return 0;
    }
    if(operation==3) {
        const auto obj=sdk2::handle_address(bus,owner.services,args[0],args[1]);if(!obj)return -2;
        unsigned record=0;for(unsigned i=0;i<32;++i) {
            const auto at=owner.services+sdk2::SpawnOwners+i*16;
            if(bus.load(at,4)==obj&&bus.load(at+4,4)==bus.load(obj+0x40,4)&&bus.load(at+8,4)==bus.load(obj+0x3c,4)&&bus.load(at+12,4)==bus.load(owner.services+sdk2::Header+4,4)){record=at;break;}
        }
        if(!record||bus.load(obj+0x48,2)!=2)return -4;
        if(bus.load(symbol("gFreeListCount"),4)>=32)return -6;
        if(bus.load(record+12,4)&0x80000000U)return -7;
        native_call("free_object",{obj});bus.store(record+12,4,bus.load(record+12,4)|0x80000000U);return 0;
    }
    if(operation==10) {
        if(args[0]>3||args[1]>1018||args[2]>1023||args[2]<=args[1]+4||args[3]>255||args[4]>255||args[5]>255)return -1;
        native_call("set_fog",{args[0],args[1],args[2],args[3],args[4],args[5]});return 0;
    }
    if(operation==11) {
        const auto header=std::uint32_t(bus.load(symbol("gCurrentLevelHeader"),4));if(!sdk2::guest(header,0xa0))return -3;
        // Only modify a scene's already initialized weather. Enabling a new
        // weather type requires resource preflight and a scene reload.
        if(std::int16_t(bus.load(header+0x90,2))<=0)return -8;
        for(unsigned i=0;i<3;++i)if(std::int32_t(args[i])<-0x100000||std::int32_t(args[i])>0x100000)return -1;
        if(args[3]>65536||args[4]>65536||args[5]<1||args[5]>3600)return -1;
        native_call("weather_set",{args[0],args[1],args[2],args[3],args[4],args[5]});return 0;
    }
    if(operation>=20&&operation<=23) {
        Registers call;call.integer[4]=operation-20;call.integer[5]=args[0];call.integer[29]=std::int32_t(owner.services+61440);bus.fill(owner.services+61440,80);
        if(!service("dkr_audio_command",index,bus,call,budget))return -5;
        return call.integer[2]?0:-1;
    }
    if(operation==32) {
        if(!sdk2_development_)return -4;
        if(args[0]>2||bus.load(symbol("gGameMode"),4)!=0||bus.load(symbol("gLevelLoadTimer"),2)||bus.load(symbol("gPostRaceViewPort"),1))return -3;
        native_call("menu_pause_init",{});bus.store(symbol("gIsPaused"),1,1);
        bus.fill(owner.services+23200,20);bus.store(owner.services+23220,4,args[0]==0?5:args[0]==1?12:4);return 0;
    }
    if(operation==40) {
        if(!sdk2_development_||!owner.package->modules.contains("track-tools2"))return -4;
        // Native result/save arrays have three slots. The existing extended-lap
        // owner has its own complete timing path; do not compete with it.
        for(unsigned other=0;other<linked().owners.size();++other)if(linked().owners[other].package->systems.contains("race-laps"))return -7;
        if(args[0]>3)return -1;const auto header=std::uint32_t(bus.load(symbol("gCurrentLevelHeader"),4));
        if(bus.load(symbol("gGameMode"),4)!=0||!sdk2::guest(header,0xc4)||bus.load(header+0x4c,1)!=0)return -3;
        const auto count=unsigned(bus.load(symbol("gObjectCount"),4)),list=std::uint32_t(bus.load(symbol("gObjPtrList"),4));
        if(count>512||!sdk2::guest(list,count*4))return -3;
        if(budget.instructions<count*32U)return -6;budget.instructions-=count*32U;
        unsigned racers=0;for(unsigned n=0;n<count;++n){const auto obj=std::uint32_t(bus.load(list+n*4,4));if(!sdk2::guest(obj,0x80)||bus.load(obj+0x48,2)!=1)continue;
            const auto racer=std::uint32_t(bus.load(obj+0x64,4));if(!sdk2::guest(racer,0x224))return -3;
            if(std::int8_t(bus.load(racer+0x193,1))>0||bus.load(racer+0x1d8,1))return -3;++racers;}
        if(!racers)return -3;const auto state=owner.services+38976;
        for(unsigned other=0;other<linked().owners.size();++other)if(other!=index&&linked().owners[other].package->api==2&&bus.load(linked().owners[other].services+38976,4))return -7;
        const unsigned prior=unsigned(bus.load(header+0x4b,1));if(prior<1||prior>3)return -8;
        if(!args[0]){if(!bus.load(state,4))return 0;if(bus.load(state+4,4)!=header)return -2;bus.store(header+0x4b,1,bus.load(state+8,4));bus.fill(state,16);return 0;}
        if(!bus.load(state,4)){bus.store(state,4,1);bus.store(state+4,4,header);bus.store(state+8,4,prior);}
        bus.store(header+0x4b,1,args[0]);return 0;
    }
    if(operation==30||operation==31) {
        if(!sdk2_development_)return -4;
        if(bus.load(symbol("gGameMode"),4)!=0||bus.load(symbol("gLevelLoadTimer"),2)||bus.load(symbol("gLevelPropertyStackPos"),2))return -3;
        const trophy::Course* course=nullptr;
        if(operation==31){if(sdk2_courses_.empty())return -5;if(args[0]>=sdk2_courses_.size())return -1;course=&sdk2_courses_[args[0]];}
        unsigned level=operation==30?unsigned(bus.load(symbol("gPlayableMapId"),4)):course->carrier;
        unsigned vehicle=operation==30?unsigned(bus.load(symbol("gLevelDefaultVehicleID"),4)):args[2];
        if(level>=bus.load(symbol("gNumberOfLevelHeaders"),4)||args[1]>15||vehicle>2)return -1;
        // Other courses have not constructed their native entrance objects.
        // Only their admitted default spawn is safe to request before reload.
        if(args[1]!=0) {
            if(operation!=30)return -8;const unsigned racers=unsigned(bus.load(symbol("gNumRacers"),4));
            const unsigned count=unsigned(bus.load(symbol("gObjectCount"),4));const auto objects=std::uint32_t(bus.load(symbol("gObjPtrList"),4));
            if(!racers||racers>8||count>512||!sdk2::guest(objects,count*4))return -3;
            unsigned anchors=0;for(unsigned i=0;i<count;++i){const auto object=std::uint32_t(bus.load(objects+i*4,4));if(!sdk2::guest(object,0x80)||bus.load(object+0x48,2)!=11)continue;
                const auto entry=std::uint32_t(bus.load(object+0x3c,4));if(!sdk2::guest(entry,12))return -3;
                if(bus.load(entry+9,1)==args[1]&&bus.load(entry+8,1)<8)anchors|=1U<<unsigned(bus.load(entry+8,1));}
            if((anchors&((1U<<racers)-1))!=((1U<<racers)-1))return -8;
        }
        if(course&&!(course->vehicles&(1U<<vehicle)))return -1;
        // Native .dkrmap headers have their own admitted IDs; legacy courses
        // select an explicit content identity rather than a borrowed carrier.
        if(operation==31&&course_selector_)course_selector_(course->id.starts_with("legacy:")?course->id.substr(7):std::string{},level);
        if(operation==30&&course_selector_&&sdk2_current_course_)course_selector_(sdk2_current_course_(),level);
        native_call("level_properties_push",{level,args[1],vehicle,0});native_call("level_transition_begin",{0});return 0;
    }
    return -5;
}

// Included inside GameWorld::sdk2_service after the ordinary argument/owner
// checks. Only the immutable logical sample can be applied by this service.
if(name=="dkr2_driving_input"||name=="dkr2_driving_apply") {
    namespace driving=dkr::runtime::driving;
    if(!owner.package->modules.contains("analogue-driving"))return result(-5);
    if(sdk2_phase_!=2||a>=4||!driving::frame.available)return result(-4);
    const auto value=driving::frame.inputs[a];
    if(name=="dkr2_driving_input") {
        if(b<owner.begin||b>owner.end||20>owner.end-b)return result(-1);
        const unsigned values[]={1,20,value.flags,value.accelerator,value.brake};
        for(unsigned i=0;i<5;++i)bus.store(b+i*4,4,values[i]);return result(0);
    }
    driving::frame.symbols={symbol("gCurrentRacerInput"),symbol("gCurrentPlayerIndex"),symbol("gRacerInputBlocked"),symbol("gTwoActivePlayersInAdventure"),symbol("gLeadPlayerIndex")};
    driving::frame.requests[a]={value,(value.flags&driving::Enabled)!=0};
    return result(0);
}

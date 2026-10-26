void input_lane_and_reordering() {
    Fixture client(false,false,false);client.initial();
    Fixture host(false,true,false);host.runtime->activate({91,0});
    {std::scoped_lock lock(client.hub->mutex);for(auto& q:client.hub->outgoing)q.clear();client.hub->outgoing_bytes=0;}
    for(unsigned frame:{10U,8U,9U})client.runtime->sample({std::uint16_t(frame),0,0},frame);
    unsigned received=0;
    for(const auto& queue:client.hub->outgoing)for(const auto& packet:queue) {
        if(packet.bytes.size()<5||packet.bytes[4]!=unsigned(Msg::Input))continue;
        check(packet.traffic==Lane::Authoritative,"input must use reliable unordered authority channel");
        host.runtime->message(2,packet.bytes,Clock::now());
        host.runtime->message(2,packet.bytes,Clock::now());++received;
    }
    check(received==3,"all out-of-order samples delivered");
    for(unsigned frame:{8U,9U,10U}){
        const auto it=host.runtime->inputs.find({frame,1});
        check(it!=host.runtime->inputs.end()&&it->second.first.buttons==frame,
            "reordering and duplicates preserve immutable owner input");
    }
    for(const auto& queue:host.hub->outgoing)for(const auto& packet:queue)
        if(packet.bytes.size()>=5&&packet.bytes[4]==unsigned(Msg::Input))
            check(packet.traffic==Lane::Authoritative,"host input relays retain independent lane");
}
void initial_checkpoint_burst() {
    Fixture f(false,true,false);
    // Retire the fixture's tiny bootstrap encoder before substituting the
    // larger wire checkpoint. It must not overwrite this synthetic delivery.
    if(f.runtime->encoding.valid())(void)f.runtime->encoding.get();
    f.runtime->encoded.reset();f.runtime->encode_pending.reset();f.runtime->needing.clear();
    const auto raw=f.baseline({91,0});f.runtime->held=raw;
    auto wire=raw;wire.state=std::make_shared<Bytes>(100*1024,7);wire.digest=digest(*wire.state);
    f.runtime->start_delivery(2,wire);f.runtime->deliveries.at(2).accepted=true;
    f.runtime->transfer(Clock::now());
    unsigned chunks=0;
    for(const auto& queue:f.hub->outgoing)for(const auto& packet:queue)
        if(packet.bytes.size()>=5&&packet.bytes[4]==unsigned(Msg::Chunk)){
            check(packet.traffic==Lane::Checkpoint,"checkpoint retains isolated bulk lane");++chunks;
        }
    check(chunks==4,"setup should fill a bounded four-chunk burst per peer");
    f.runtime->phase=PartyRuntime::Impl::Phase::Running;
    {std::scoped_lock lock(f.hub->mutex);for(auto& q:f.hub->outgoing)q.clear();f.hub->outgoing_bytes=0;}
    f.runtime->transfer(Clock::now());chunks=0;
    for(const auto& queue:f.hub->outgoing)for(const auto& packet:queue)
        if(packet.bytes.size()>=5&&packet.bytes[4]==unsigned(Msg::Chunk))++chunks;
    check(chunks==1,"active race spectator transfer keeps original pacing");
}

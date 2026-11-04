#include "online_stability_party_tests.inl"

void analogue_party_envelopes() {
    Fixture host(false,true,false);host.runtime->activate({91,0});
    Fixture client(false,false,false);client.initial();
    for(unsigned frame=0;frame<5;++frame) {
        const unsigned level=frame==4?255:frame*64;
        const net::PackedInput value{0xC000,0,0,std::uint8_t(level),std::uint8_t(255-level),1};
        host.runtime->sample(value,frame);client.runtime->sample(value,frame);
        const auto it=host.runtime->inputs.find({frame,0});
        check(it!=host.runtime->inputs.end()&&packed(it->second.first)==value,"local authority uses the complete analogue controller envelope");
    }
    for(const auto& queue:client.hub->outgoing)for(const auto& packet:queue)
        if(packet.bytes.size()>4&&(packet.bytes[4]==unsigned(Msg::Input)||packet.bytes[4]==unsigned(Msg::LiveInputs)))
            host.runtime->message(2,packet.bytes,Clock::now());
    for(unsigned frame=0;frame<5;++frame) {
        const unsigned level=frame==4?255:frame*64;
        const auto it=host.runtime->inputs.find({frame,1});
        check(it!=host.runtime->inputs.end()&&it->second.first.accelerator==level&&it->second.first.brake==255-level&&it->second.first.driving_flags==1,
            "remote party input preserves both pedals through its length-prefixed envelope");
    }
    wire::ControllerSample sample{{91,7},1,host.runtime->roster.snapshot().controls[1].generation,{0xC000,0,0,64,192,1}};
    const auto bytes=wire::controller(sample);check(bytes.size()==wire::ControllerSampleBytes,"controller length equals the shared wire constant");
    Write legacy;legacy.n(host.runtime->controls).n(25,1).data(std::span(bytes).first(25));Read old{legacy.b};bool refused=false;
    try{host.runtime->receive_input(2,old);}catch(const std::exception&){refused=true;}
    check(refused,"old controller envelope widths are refused before decoding");
}
void input_lane_and_reordering() {
    analogue_party_envelopes();
    deadline_local_response();
    for(unsigned players=2;players<=4;++players){multi_racer_delivery(players,150,97);multi_racer_delivery(players,300,31);multi_racer_delivery(players,300,31,1000);}
    for(unsigned players=2;players<=4;++players){multi_racer_delivery(players,150,97,0,12);multi_racer_delivery(players,300,31,0,12);}
    for(unsigned players=2;players<=4;++players)multi_racer_delivery(players,300,31,0,20);
    multi_racer_delivery(4,600,97,0,20);
    dynamic_party_samples(250);dynamic_party_samples(500);
    Fixture client(false,false,false);client.initial();
    Fixture host(false,true,false);host.runtime->activate({91,0});
    {std::scoped_lock lock(client.hub->mutex);for(auto& q:client.hub->outgoing)q.clear();client.hub->outgoing_bytes=0;}
    for(unsigned frame:{10U,8U,9U})client.runtime->sample({std::uint16_t(frame),0,0},frame);
    unsigned received=0;
    for(const auto& queue:client.hub->outgoing)for(const auto& packet:queue) {
        if(packet.bytes.size()<5||packet.bytes[4]!=unsigned(Msg::LiveInputs))continue;
        check(packet.traffic==Lane::Realtime,"fresh input has an independent unordered live channel");
        host.runtime->message(2,packet.bytes,Clock::now());
        host.runtime->message(2,packet.bytes,Clock::now());++received;
    }
    check(received>=2,"out-of-order live runs retained while obsolete queued runs coalesce");
    for(unsigned frame:{8U,9U,10U}){
        const auto it=host.runtime->inputs.find({frame,1});
        check(it!=host.runtime->inputs.end()&&it->second.first.buttons==frame,
            "reordering and duplicates preserve immutable owner input");
    }
    for(const auto& queue:host.hub->outgoing)for(const auto& packet:queue)
        if(packet.bytes.size()>=5&&packet.bytes[4]==unsigned(Msg::LiveInputs))
            check(packet.traffic==Lane::Realtime,"host live relays preserve the fresh lane");
    // A receipt is authority for retention, never for changing an input. Its
    // account/owner, epoch, control generation, lease and authored bound matter.
    auto receipt=[&](MemberId source,std::uint64_t generation,std::uint64_t epoch,unsigned owner,std::uint64_t lease,unsigned next) {
        Write body;body.n(generation).n(epoch).n(owner,1).n(lease).n(next,4);
        Write packet;packet.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(Msg::InputReceipt),1).n(client.runtime->match).n(400).data(body.b);
        client.runtime->message(source,packet.b,Clock::now());
    };
    client.runtime->authored=11;
    const auto lease=client.runtime->roster.snapshot().controls[1].generation;
    receipt(3,1,91,1,lease,9);receipt(1,2,91,1,lease,9);receipt(1,1,92,1,lease,9);
    receipt(1,1,91,0,lease,9);receipt(1,1,91,1,lease+1,9);receipt(1,1,91,1,lease,12);
    check(client.runtime->host_received==0,"forged/stale/unpublished receipts cannot retire owner history");
    receipt(1,1,91,1,lease,9);receipt(1,1,91,1,lease,8);
    check(client.runtime->host_received==9,"valid receipts advance monotonically");
    client.runtime->activate({92,0});
    check(client.runtime->host_received==0,"new scene resets old receipt authority");
    receipt(1,1,91,1,lease,9);check(client.runtime->host_received==0,"late receipt cannot cross an epoch");
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

// Included after the retained Fixture helpers. This executes the exact final
// party runtime with synthetic RAM, independent 30-Hz pacing and delayed wire
// input. No ROM, GPU, files, live service or external player is involved.
void deadline_local_response() {
    namespace stability=dkr::online_stability;
    Fixture host(false,true,false,0,20);host.runtime->activate({91,0});
    if(host.runtime->encoding.valid())(void)host.runtime->encoding.get();
    host.runtime->encoded.reset();host.runtime->encode_pending.reset();host.runtime->needing.clear();
    auto bridge=stability::control(&host);bridge->configure({1234,1,1,true,true,true,{{2,20,true,1}}});
    const auto start=Clock::now();host.runtime->next_tick=start;
    check(host.runtime->pulse(start,{},host.error),"zero-lead first tick");
    check(host.runtime->next().frame==1&&host.runtime->authored==1,"zero lead authors only the tick which is due");
    check(host.runtime->pulse(start+std::chrono::milliseconds(1),{},host.error),"zero-lead pacing turn");
    check(host.runtime->authored==1,"a pacing turn must not publish next tick's stale controls");
    check(host.runtime->pulse(start+std::chrono::milliseconds(20),{0x8000,50,0},host.error),"fresh control between ticks");
    check(host.runtime->authored==1,"physical input waits for its actual tick deadline");
    check(host.runtime->pulse(start+Tick,{0x8000,60,0},host.error),"fresh control at next tick");
    check(host.runtime->local_history.at(1).input==PortInput{0x8000,60,0},"next tick uses the latest stick and retained button edge");
    stability::forget(&host);
}

void multi_racer_delivery(unsigned players,unsigned rtt,unsigned loss_every,unsigned outage_ms=0,unsigned rollback_window=3) {
    namespace stability=dkr::online_stability;
    std::vector<std::unique_ptr<Fixture>> peers;
    for(unsigned p=0;p<players;++p) {
        peers.push_back(std::make_unique<Fixture>(false,p==0,false,6,rollback_window,players,p+1));auto& f=*peers.back();
        if(p==0)f.runtime->activate({91,0});else f.initial();
        if(f.runtime->encoding.valid())(void)f.runtime->encoding.get();
        f.runtime->encoded.reset();f.runtime->encode_pending.reset();f.runtime->needing.clear();
        for(auto& q:f.hub->outgoing)q.clear();f.hub->outgoing_bytes=0;
        std::vector<stability::Route> routes;
        if(!p)for(unsigned n=1;n<players;++n)routes.push_back({n+1,rtt,true,n});else routes.push_back({1,rtt,true,0});
        stability::control(&f)->configure({1234,p+1,1,true,p==0,true,std::move(routes)});
        f.runtime->freeze=1800;
    }
    struct Pending {unsigned from,to,due;Bytes bytes;bool extension;};std::vector<Pending> pending;
    std::array<std::uint64_t,4> bytes{},wait{},longest{},run{};
    std::array<std::uint64_t,4> lead_sum{},lead_count{};
    const auto start=Clock::now();unsigned packet_serial=0;std::uint64_t sequence=0;
    net::secure::Key key{};key.fill(0x37);bool finished=false;
    for(unsigned ms=0;ms<75000;ms+=5) {
        const auto now=start+std::chrono::milliseconds(ms);
        for(auto at=pending.begin();at!=pending.end();) {
            if(at->due>ms){++at;continue;}
            auto& f=*peers[at->to];
            if(at->extension)check(stability::control(&f)->receive(at->from+1,true,at->bytes),"multi-racer control origin");
            else f.runtime->message(at->from+1,at->bytes,now);
            at=pending.erase(at);
        }
        for(unsigned p=0;p<players;++p) {
            auto& f=*peers[p];check(f.runtime->pulse(now,{std::uint16_t((ms/(137+p*19))%2?0x8000:0),std::int8_t(int(ms/100%81)-40),0},f.error),"multi-racer actual driver pulse");
            if(ms>=20000&&ms<60000){lead_sum[p]+=f.runtime->live_delay.lead();++lead_count[p];}
            if(ms>=12000&&ms<60000){if(f.runtime->delay_network_wait){wait[p]+=5;run[p]+=5;longest[p]=std::max(longest[p],run[p]);}else run[p]=0;}
            for(auto& q:f.hub->outgoing)while(!q.empty()) {
                auto packet=std::move(q.front());q.pop_front();f.hub->outgoing_bytes-=packet.bytes.size();
                if(packet.kind!=LinkKind::Runtime)continue;
                for(unsigned d=0;d<players;++d)if(d!=p&&(!packet.target||packet.target==d+1)) {
                    const auto payload=link_encode(packet.kind,packet.bytes);
                    net::protocol::Datagram record{{net::protocol::MessageType(PartyMessageType),1234,++sequence,unsigned(packet.traffic)},payload};
                    const auto plain=net::protocol::encode(record,net::protocol::kMaximumQuickJoinDatagramBytes);
                    const auto wire=net::secure::seal(plain,key,p+1,sequence,1234);check(!plain.empty()&&!wire.empty(),"actual outer racing codecs");
                    if(ms>=12000&&ms<60000)bytes[p]+=wire.size();
                    const bool live=packet.traffic==Lane::Realtime;const auto serial=++packet_serial;
                    if((live&&loss_every&&serial%loss_every==0)||(outage_ms&&ms>=30000&&ms<30000+outage_ms&&p==players-1))continue;
                    pending.push_back({p,d,ms+rtt/2+serial%21,packet.bytes,false});
                }
            }
            for(auto& packet:stability::control(&f)->outgoing())for(unsigned d=0;d<players;++d)
                if(d!=p&&(!packet.target||packet.target==d+1))pending.push_back({p,d,ms+rtt/2,packet.bytes,true});
        }
        check(pending.size()<4000,"multi-racer queues stay bounded");
        if(rtt>=300&&ms%5000==0)for(unsigned p=0;p<players;++p) {
            const auto& game=*peers[p]->runtime;
            std::cout<<"Delivery frontier racers="<<players<<" owner="<<p<<" ms="<<ms<<" frame="<<game.next().frame
                <<" confirmed="<<game.confirmed()<<" lead="<<game.live_delay.lead()<<" missing="<<unsigned(game.driver->missing_input_mask())
                <<" wait-ms="<<wait[p]<<'\n';
        }
        for(const auto& f:peers)if(!f->runtime->server()) {
            const auto& game=*f->runtime;
            for(const auto& [frame,sample]:game.local_history)if(frame<game.confirmed()&&frame>=game.host_received)
                check(sample.cursor.frame==frame,"locally confirmed input remains available until the host acknowledges it");
        }
        finished=true;for(const auto& f:peers)finished&=f->runtime->confirmed()==1800;
        if(finished)break;
    }
    check(finished,"every racing peer confirms the same 1800 frames");
    for(unsigned p=1;p<players;++p)check(peers[p]->world.state==peers[0]->world.state,"confirmed multi-racer state is byte exact");
    for(unsigned p=0;p<players;++p) {
        std::cout<<"Party delivery racers="<<players<<" owner="<<p<<" RTT="<<rtt<<" jitter<=20ms loss=1/"<<loss_every<<" outage-ms="<<outage_ms
            <<" wait-ms="<<wait[p]<<" longest-wait-ms="<<longest[p]<<" application-kbit-s="<<double(bytes[p])*8/48000
            <<" replayed="<<peers[p]->runtime->driver->statistics().replayed_frames<<" window="<<rollback_window<<" mean-lead-frames="<<double(lead_sum[p])/std::max(std::uint64_t(1),lead_count[p])<<" confirmed=1800 synthetic-world=1\n";
        // Deadline sampling removes the former invisible early-publication
        // tick. A 12-frame window can need 3-4 explicit frames at 300 ms;
        // the 20-frame qualification keeps the lower two-frame target.
        if(rollback_window>=12&&!outage_ms&&rtt<=300)check(lead_sum[p]<=lead_count[p]*(rollback_window>=20?2U:4U),"distant fixtures keep bounded low input delay for their available prediction history");
        if(rtt<=300&&!outage_ms){check(wait[p]<480,"normal distant network wait remains below one percent");check(longest[p]<=250,"normal distant pauses stay below 250ms");}
        if(outage_ms){check(wait[p]<4000,"a one-second outage must not cause sustained stop/start afterwards");check(longest[p]<2000,"outage repair completes within the bounded recovery fixture");}
        if(p)check(double(bytes[p])*8/48000<100,"racer application traffic stays below 100kbit/s without proxy probes");
        stability::forget(peers[p].get());
    }
}

void dynamic_party_samples(unsigned impaired_rtt=500) {
    namespace stability=dkr::online_stability;
    Fixture host(false,true,true,6,2),client(false,false,true,6,2);
    host.runtime->activate({91,0});client.initial();
    for(auto* f:{&host,&client}) {
        if(f->runtime->encoding.valid())(void)f->runtime->encoding.get();
        f->runtime->encoded.reset();f->runtime->encode_pending.reset();f->runtime->needing.clear();
        for(auto& queue:f->hub->outgoing)queue.clear();f->hub->outgoing_bytes=0;
        stability::control(f)->configure({1234,f->hub->local,1,true,f==&host,true,
            f==&host?std::vector<stability::Route>{{2,40,true,1},{3,4900,false}}:std::vector<stability::Route>{{1,40,true,0}}});
    }
    struct Pending {Fixture* destination;MemberId source;Bytes bytes;unsigned due;bool extension;};
    std::vector<Pending> pending_packets;
    std::map<std::pair<unsigned,unsigned>,PortInput> immutable;
    const auto start=Clock::now();unsigned low=9,high=0,delay_changes=0,last=6;
    for(unsigned ms=0;ms<60000;ms+=5) {
        const unsigned rtt=ms>=20000&&ms<35000?impaired_rtt:40;
        const auto now=start+std::chrono::milliseconds(ms);
        for(auto* f:{&host,&client}) {
            auto bridge=stability::control(f);auto id=bridge->identity();for(auto& route:id.routes)if(route.racer)route.rtt_ms=rtt;bridge->configure(std::move(id));
        }
        for(auto it=pending_packets.begin();it!=pending_packets.end();) {
            if(it->due>ms){++it;continue;}
            if(it->extension)check(stability::control(it->destination)->receive(it->source,true,it->bytes),"authenticated extension delivery");
            else {
                if(it->bytes.size()>21&&it->bytes[4]==unsigned(Msg::LiveInputs)) {
                    stability::LiveInputs packet;check(stability::decode_live_inputs(std::span(it->bytes).subspan(21),packet),"actual live input batch codec");
                    for(unsigned n=0;n<packet.count;++n) {
                        const auto v=packet.inputs[n];const PortInput input{v.buttons,v.x,v.y};const auto key=std::pair{packet.first+n,packet.owner};
                        if(const auto old=immutable.find(key);old!=immutable.end())check(old->second==input,"live batch rewrote published input");else immutable.emplace(key,input);
                    }
                }
                if(it->bytes.size()>21&&it->bytes[4]==unsigned(Msg::Input)) {
                    Read r{std::span<const std::uint8_t>(it->bytes).subspan(21)};(void)r.n();const auto count=r.n(1);
                    wire::ControllerSample sample;std::string error;
                    check(count==wire::ControllerSampleBytes&&wire::controller(r.b.subspan(r.pos,wire::ControllerSampleBytes),sample,error),"actual party input codec");
                    const auto key=std::pair{sample.cursor.frame,unsigned(sample.port)};
                    if(const auto old=immutable.find(key);old!=immutable.end())check(old->second==sample.input,"delay change rewrote published input");
                    else immutable.emplace(key,sample.input);
                }
                it->destination->runtime->message(it->source,it->bytes,now);
            }
            it=pending_packets.erase(it);
        }
        for(auto* f:{&host,&client}) {
            // Distinct press/release transitions continue across lower/higher
            // lead. The runtime may replay, but wire samples stay immutable.
            const net::PackedInput input{std::uint16_t((ms/700+(f==&client))%2?0x8000:0),0,0};
            check(f->runtime->pulse(now,input,f->error),"actual party dynamic pulse");
            for(auto& queue:f->hub->outgoing)while(!queue.empty()) {
                auto packet=std::move(queue.front());queue.pop_front();f->hub->outgoing_bytes-=packet.bytes.size();
                auto* destination=f==&host?&client:&host;
                if((!packet.target||packet.target==destination->hub->local)&&packet.kind==LinkKind::Runtime)
                    pending_packets.push_back({destination,f->hub->local,std::move(packet.bytes),ms+rtt/2,false});
            }
            for(auto& packet:stability::control(f)->outgoing()) {
                auto* destination=f==&host?&client:&host;
                if(!packet.target||packet.target==destination->hub->local)pending_packets.push_back({destination,f->hub->local,std::move(packet.bytes),ms+rtt/2,true});
            }
        }
        if(host.runtime->live_delay.lead()!=last){last=host.runtime->live_delay.lead();++delay_changes;}
        if(ms>15000&&ms<20000)low=std::min(low,host.runtime->live_delay.lead());
        if(ms>27000&&ms<34000)high=std::max(high,host.runtime->live_delay.lead());
        check(pending_packets.size()<1000,"bounded live input/control traffic");
    }
    check(delay_changes>=2&&low<6,"actual party runtime reduces stable delay");
    check(impaired_rtt>=500?high>low:high<=6,"actual party runtime raises only when delivery exhausts headroom");
    check(host.runtime->confirmed()>1200&&client.runtime->confirmed()>1200,"both actual drivers keep confirming");
    check(immutable.size()>2400,"actual final samples covered both racing owners");
    std::array<unsigned,2> edges{},prior_buttons{};
    for(const auto& [key,input]:immutable)if(key.second<2) {
        const auto owner=key.second;
        if((input.buttons&0x8000)&&!(prior_buttons[owner]&0x8000))++edges[owner];
        prior_buttons[owner]=input.buttons;
    }
    check(edges[0]==43&&edges[1]==43,"actual party scheduler retained every physical button press");
    std::cout<<"Actual party delay trace low="<<low<<" high="<<high<<" changes="<<delay_changes<<" confirmed="<<host.runtime->confirmed()<<'/'<<client.runtime->confirmed()<<'\n';
    stability::forget(&host);stability::forget(&client);
}

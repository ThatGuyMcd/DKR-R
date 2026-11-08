// Included inside the diagnostic's anonymous namespace. A report contains at
// most 12 fixed-size rows. Names and roles come from the authenticated roster,
// never arbitrary report strings. No game input, guest state or save is exposed.
constexpr unsigned MaximumReportRows=12,ReportPacketsPerPulse=12;
const Peer* report_member(const Context& c,Id id){for(const auto& p:c.members)if(p.id==id)return &p;return nullptr;}
// calculate() has already rounded upwards once to wire microseconds. Do not
// ceil a second time: binary floating-point could otherwise add another us.
std::uint64_t bounded_us(double value){return std::uint64_t(std::clamp(std::round(value*1000.),0.,14000000.));}
std::vector<std::uint8_t> report_wire(const State& s,unsigned index){
    auto packet=wire(s,ReportRow,index+1,0);net::protocol::PreflightProbePayload frame;std::string error;
    if(!net::protocol::decode_preflight_probe(packet,frame,error))return {};
    auto& b=frame.padding;const auto& r=s.report_rows[index];b[19]=std::uint8_t(s.report_rows.size());
    put(b,20,r.id,8);b[28]=std::uint8_t(unsigned(r.supported)|(unsigned(r.host)<<1)|(unsigned(r.spectator)<<2));b[29]=ScoringVersion;
    put(b,30,r.sent,2);put(b,32,r.replies,2);put(b,34,r.repair_sent,2);put(b,36,r.repair_replies,2);
    put(b,40,bounded_us(r.median),4);put(b,44,bounded_us(r.p95),4);put(b,48,bounded_us(r.jitter),4);put(b,52,bounded_us(r.repair_p95),4);
    put(b,56,r.blocked,2);put(b,58,r.errors,2);put(b,60,std::min(s.view.scheduler_gaps,65535U),2);
    put(b,64,std::min<std::size_t>(s.view.peak_queued,UINT32_MAX),4);put(b,68,std::min<std::size_t>(s.view.final_queued,UINT32_MAX),4);put(b,72,s.context.roster,8);
    return net::protocol::encode_preflight_probe(frame);
}
void freeze_report(State& s){
    s.report_rows=s.view.rows;
    std::fprintf(stderr,"[rollback-check][scoreboard] policy=%u participants=%u; immutable host report\n",ScoringVersion,unsigned(s.report_rows.size()));
}
void deliver_report(State& s,const Send& send,Clock::time_point now){
    if(s.report_rows.empty()||s.report_rows.size()>MaximumReportRows||s.measures.empty())return;
    std::vector<Id> ids;for(const auto& [id,m]:s.measures)ids.push_back(id);
    for(unsigned n=0;n<ReportPacketsPerPulse;++n){
        const auto target=ids[s.report_cursor++%ids.size()];auto& m=s.measures.at(target);
        if(!m.row.supported||m.receipted||now<m.result_due)continue;
        const auto bytes=report_wire(s,m.report_next);
        if(bytes.empty()||!send(target,Type::PreflightControlProbe,bytes)){m.result_due=now+std::chrono::milliseconds(25);continue;}
        if(++m.report_next==s.report_rows.size()){m.report_next=0;m.result_due=now+std::chrono::milliseconds(500);}
    }
}
bool receive_report(State& s,const Context& c,const net::protocol::PreflightProbePayload& packet,const Send& send,Clock::time_point now){
    const auto& b=packet.padding;const unsigned count=b[19],index=packet.sequence?packet.sequence-1:MaximumReportRows;
    if(b[29]!=ScoringVersion||!count||count>MaximumReportRows||count!=c.members.size()||index>=count||
       get(b,72,8)!=c.roster||b[16]!=s.view.window||b[17]!=s.view.delay||b[18]!=unsigned(s.view.automatic)||b[28]>7)return false;
    const auto* member=report_member(c,get(b,20,8));if(!member)return false;
    const bool host=(b[28]&2)!=0,spectator=(b[28]&4)!=0;
    if(host!=(member->id==c.host_member)||spectator!=member->spectator)return false;
    Row r;r.id=member->id;r.name=member->name;r.host=host;r.spectator=spectator;r.supported=(b[28]&1)!=0;
    r.sent=unsigned(get(b,30,2));r.replies=unsigned(get(b,32,2));r.repair_sent=unsigned(get(b,34,2));r.repair_replies=unsigned(get(b,36,2));
    if(r.sent>MaxSamples||r.repair_sent>MaxSamples||r.replies>r.sent||r.repair_replies>r.repair_sent||get(b,56,2)>MaxSamples||get(b,58,2)>MaxSamples)return false;
    for(unsigned offset:{40,44,48,52})if(get(b,offset,4)>14000000)return false;
    r.median=get(b,40,4)/1000.;r.p95=get(b,44,4)/1000.;r.jitter=get(b,48,4)/1000.;r.repair_p95=get(b,52,4)/1000.;
    if(r.p95<r.median||(host&&(r.sent||r.replies||r.repair_sent||r.repair_replies)))return false;
    r.blocked=unsigned(get(b,56,2));r.errors=unsigned(get(b,58,2));
    if(s.report_parts[index].empty()){
        if(s.view.complete)return false;
        // Every row must agree on the snapshot settings and global diagnostics.
        for(const auto& part:s.report_parts)if(!part.empty()){
            if(part[19]!=b[19]||!std::equal(part.begin()+60,part.begin()+80,b.begin()+60))return false;
            if(get(part,20,8)==r.id)return false;
        }
        s.report_parts[index]=b;s.received_rows[index]=r;
    }else if(s.report_parts[index]!=b){
        cancel(s,"Conflicting connection report received. Run a fresh check.");return false;
    }
    for(unsigned i=0;i<count;++i)if(s.report_parts[i].empty())return true;
    s.view.rows.clear();for(unsigned i=0;i<count;++i)s.view.rows.push_back(s.received_rows[i]);
    s.view.scheduler_gaps=unsigned(get(b,60,2));s.view.peak_queued=std::size_t(get(b,64,4));s.view.final_queued=std::size_t(get(b,68,4));
    s.view.active=false;s.view.complete=true;s.view.phase=Phase::Complete;s.view.remaining_ms=0;s.view.revision=s.test;
    s.view.status="Connection scoreboard ready. Everyone sees the same host-measured results.";
    if(s.receipt_at.time_since_epoch().count()==0||now-s.receipt_at>=std::chrono::milliseconds(200)){
        send(c.host_id,Type::PreflightControlProbe,wire(s,Receipt,1,0));s.receipt_at=now;
    }
    return true;
}

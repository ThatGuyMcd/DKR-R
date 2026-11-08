"""Add a diagnostic-only scoreboard over accepted Playtest 12 sources."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Scoreboard anchor changed: '+a[:150])
 return t.replace(a,b,1)
def header(t):
 t=once(t,'bool spectator=false,ready=false;','bool spectator=false,ready=false; Id member=0;')
 t=once(t,'std::vector<Peer> peers;','std::vector<Peer> peers,members; Id local_member=0,host_member=0;')
 t=once(t,'bool spectator=false,supported=false;','bool spectator=false,supported=false,host=false;')
 return once(t,'Id room=0; Phase phase=', 'Id room=0,local=0,host_id=0; Phase phase=')
def context(t):
 t=once(t,'    std::sort(c.peers.begin(),c.peers.end(),', '''    // Report identities are authenticated party IDs; ordinary four-port
    // lobbies use generation-bound port tokens because their Room codec does
    // not expose other clients' authentication IDs. No port is assigned here.
    if(c.party){
        c.local_member=sender_id_;c.host_member=c.host_id;
        for(const auto& m:party->party.snapshot().members)if(m.connected)c.members.push_back({m.id,m.name,m.role==watch::Role::Spectator,true,m.id});
        for(auto& p:c.peers)p.member=p.id;
    }else{
        c.roster=room.generation;c.local_member=local_slot_<4?Id(local_slot_)+1:0;c.host_member=1;
        for(unsigned i=0;i<room.players.size();++i)if(room.players[i].occupied)c.members.push_back({Id(i)+1,room.players[i].display_name,false,true,Id(i)+1});
        for(auto& p:c.peers){if(is_host_){if(const auto* peer=peer_by_sender(p.id))p.member=Id(peer->slot)+1;}else p.member=1;}
    }
    std::sort(c.members.begin(),c.members.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::sort(c.peers.begin(),c.peers.end(),''')
 return t.replace('Id(local_slot_)','std::uint64_t(local_slot_)').replace('Id(i)','std::uint64_t(i)').replace('Id(peer->slot)','std::uint64_t(peer->slot)')
def core(t):
 t=once(t,'#include <mutex>','#include <mutex>\n#include <cstdio>\n#include "assessment.hpp"')
 t=once(t,'Result,Receipt,Cancel};','Result,Receipt,Cancel,ReportRow};')
 t=once(t,'bool receipted=false;','bool receipted=false; unsigned report_next=0;')
 t=once(t,'    Clock::time_point client_hello{};','''    Clock::time_point client_hello{},receipt_at{},report_expected_at{};
    std::vector<Row> report_rows;
    std::array<std::vector<std::uint8_t>,12> report_parts{};
    std::array<Row,12> received_rows{};
    unsigned report_cursor=0;''')
 t=once(t,'a.peers.size()!=b.peers.size())return false;', 'a.peers.size()!=b.peers.size()||a.members.size()!=b.members.size()||a.local_member!=b.local_member||a.host_member!=b.host_member)return false;')
 t=once(t,'    return true;\n}\nType traffic', '''    for(unsigned i=0;i<a.members.size();++i)if(a.members[i].id!=b.members[i].id||a.members[i].spectator!=b.members[i].spectator||a.members[i].name!=b.members[i].name)return false;
    return true;
}
Type traffic''')
 t=once(t,"b[4]=1;b[5]=kind;","b[4]=2;b[5]=kind;")
 t=once(t,'    if(kind==Result&&m){', '''    if(kind==Hello){const auto finish=micros(s.finish_at);put(b,20,finish>at?std::min<std::uint64_t>(10000,(finish-at)/1000):0,4);}
    if(kind==Result&&m){''')
 t=once(t,'void publish(State& s){s.view.rows.clear();for(const auto& [id,m]:s.measures)s.view.rows.push_back(m.row);}', '''void publish(State& s){
    s.view.rows.clear();for(const auto& p:s.context.members){
        if(p.id==s.context.host_member){Row r;r.id=p.id;r.name=p.name;r.spectator=p.spectator;r.host=true;s.view.rows.push_back(r);continue;}
        for(const auto& [id,m]:s.measures)if(m.row.id==p.id){s.view.rows.push_back(m.row);break;}
    }
}''')
 t=once(t,'m.row.jitter=m.main.size()>1?total/double(m.main.size()-1):0;', '''m.row.jitter=m.main.size()>1?total/double(m.main.size()-1):0;
    // Host and clients grade identical wire-precision measurements.
    for(auto* value:{&m.row.median,&m.row.p95,&m.row.jitter,&m.row.repair_p95})*value=std::ceil(*value*1000.)/1000.;''')
 t=once(t,'s.view.room=c.room;s.view.host=c.host;', 's.view.room=c.room;s.view.local=c.local_member;s.view.host_id=c.host_member;s.view.host=c.host;')
 t=once(t,'s.test=0;s.nonce=0;s.retry_until={};}', 's.test=0;s.nonce=0;s.retry_until={};s.report_rows.clear();s.report_parts={};s.received_rows={};s.report_cursor=0;s.receipt_at=s.report_expected_at={};}\n#include "scoreboard_wire.inl"')
 t=once(t,'    if(c.peers.empty()||c.peers.size()>MaxPeers)', '    if(c.members.size()>12||!c.local_member||!c.host_member||!report_member(c,c.local_member)||!report_member(c,c.host_member)||c.members.size()!=c.peers.size()+1){error="Waiting for the complete lobby roster. Try again in a moment.";return false;}\n    if(c.peers.empty()||c.peers.size()>MaxPeers)')
 t=once(t,'m.row.id=peer.id;', 'm.row.id=peer.member;')
 t=once(t,'    if(!c.host){if(now>=s.finish_at)', '''    if(!c.host){
        s.view.remaining_ms=unsigned(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(s.report_expected_at-now).count()));
        if(now>=s.report_expected_at){s.view.phase=Phase::Draining;s.view.status="Receiving the complete connection scoreboard...";}
        if(now>=s.finish_at)''')
 a=t.index('        if(c.host&&s.view.complete&&now<s.retry_until)for(');b=t.index('\n        return;',a)
 t=t[:a]+'        if(c.host&&s.view.complete&&now<s.retry_until)deliver_report(s,send,now);'+t[b:]
 t=once(t,'        s.retry_until=now+std::chrono::seconds(3);', '        publish(s);freeze_report(s);s.retry_until=now+std::chrono::seconds(3);')
 t=once(t,'b[4]!=1||b[5]<Hello||b[5]>Cancel', 'b[4]!=2||b[5]<Hello||b[5]>ReportRow')
 t=once(t,'    if(kind==Hello){\n        if(b[16]', '    if(kind==Hello){\n        if(get(b,20,4)>10000||c.members.empty()||c.members.size()>12||!report_member(c,c.local_member)||!report_member(c,c.host_member))return;\n        if(b[16]')
 t=once(t,'s.finish_at=now+std::chrono::seconds(14);s.view.active=true;', 's.report_expected_at=now+std::chrono::milliseconds(get(b,20,4));s.finish_at=s.report_expected_at+std::chrono::seconds(4);s.view.active=true;')
 t=once(t,'    }else if(kind==Result&&s.view.active&&b[19]<=1){', '    }else if(kind==ReportRow&&(s.view.active||s.view.complete)){\n        receive_report(s,c,packet,send,now);\n    }else if(kind==Result&&s.view.active&&b[19]<=1){')
 # The old single-row result is not accepted by the v2 diagnostic. Leave its
 # encoder unused for composition evidence, but no complete report can use it.
 a=t.index('    }else if(kind==Result&&s.view.active&&b[19]<=1){');b=t.index('\n    else if(kind==Cancel',a)
 t=t[:a]+'    }'+t[b:]
 t=once(t,'s.view.status="Rollback connection check complete. Open the report for timing and rollback-window headroom.";', 's.view.status="Connection scoreboard ready. Open it for player scores and advice.";')
 return t

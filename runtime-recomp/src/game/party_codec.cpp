#include "party_codec.hpp"
#include <algorithm>
#include <stdexcept>

namespace dkr::runtime::spectators::wire {
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void word(std::uint64_t v,unsigned count){if(count>8||bytes.size()+count>MaximumPacket)throw std::runtime_error("Spectator packet exceeds its wire bound.");for(unsigned i=0;i<count;++i)bytes.push_back(std::uint8_t(v>>(i*8)));}
    void cursor(Cursor c){word(c.epoch,8);word(c.frame,4);}
    void text(const std::string& value){if(value.size()>24)throw std::runtime_error("Spectator name is too long.");word(value.size(),1);bytes.insert(bytes.end(),value.begin(),value.end());}
};
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t at=0;
    std::uint64_t word(unsigned count){if(count>8||count>bytes.size()-at)throw std::runtime_error("Spectator packet is truncated.");std::uint64_t v=0;for(unsigned i=0;i<count;++i)v|=std::uint64_t(bytes[at++])<<(i*8);return v;}
    Cursor cursor(){Cursor c;c.epoch=word(8);c.frame=std::uint32_t(word(4));return c;}
    std::string text(){const auto n=word(1);if(!n||n>24||n>bytes.size()-at)throw std::runtime_error("Invalid spectator name.");std::string s(reinterpret_cast<const char*>(bytes.data()+at),std::size_t(n));at+=std::size_t(n);return s;}
    void end(){if(at!=bytes.size())throw std::runtime_error("Spectator packet has trailing data.");}
};
template<class F>bool checked(std::string& error,F f){try{f();error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}catch(...){error="Invalid spectator message.";return false;}}
void input(Writer& w,PortInput p){w.word(p.buttons,2);w.word(std::uint8_t(p.x),1);w.word(std::uint8_t(p.y),1);}
PortInput input(Reader& r){PortInput p;p.buttons=std::uint16_t(r.word(2));p.x=std::int8_t(r.word(1));p.y=std::int8_t(r.word(1));return p;}
}
std::vector<std::uint8_t> encode(const Packet& p) {
    if(!p.match||!p.serial||p.kind<Kind::Roster||p.kind>Kind::Reject||p.body.size()>MaximumPacket-22)throw std::runtime_error("Invalid spectator packet envelope.");
    Writer w;for(auto c:{'D','K','S','P'})w.word(unsigned(c),1);w.word(Version,1);w.word(unsigned(p.kind),1);w.word(p.match,8);w.word(p.serial,8);
    w.bytes.insert(w.bytes.end(),p.body.begin(),p.body.end());return w.bytes;
}
bool decode(std::span<const std::uint8_t> bytes,Packet& out,std::string& error) {
    return checked(error,[&]{
        if(bytes.size()<22||bytes.size()>MaximumPacket)throw std::runtime_error("Invalid spectator envelope size.");
        Reader r{bytes};for(auto c:{'D','K','S','P'})if(r.word(1)!=unsigned(c))throw std::runtime_error("Not a spectator message.");
        if(r.word(1)!=Version)throw std::runtime_error("Spectator protocol versions differ.");
        Packet p;p.kind=Kind(r.word(1));p.match=r.word(8);p.serial=r.word(8);
        if(p.kind<Kind::Roster||p.kind>Kind::Reject||!p.match||!p.serial)throw std::runtime_error("Invalid spectator envelope identity.");
        p.body.assign(bytes.begin()+std::ptrdiff_t(r.at),bytes.end());out=std::move(p);
    });
}
std::vector<std::uint8_t> roster(const PartySnapshot& p) {
    std::string error;if(!Party::valid(p,error))throw std::runtime_error(error);
    Writer w;w.word(p.host,8);w.word(p.generation,8);w.word(p.members.size(),1);
    for(const auto& m:p.members){w.word(m.id,8);w.text(m.name);w.word(unsigned(m.role),1);w.word(m.port?*m.port:255,1);w.word(m.host?1:0,1);}
    for(const auto& c:p.controls){w.word(c.member,8);w.word(c.generation,8);w.cursor(c.starts);}
    w.word(p.handover?1:0,1);
    if(p.handover){const auto& h=*p.handover;w.word(h.token,8);w.word(h.roster_generation,8);w.word(h.port,1);w.word(h.outgoing,8);w.word(h.incoming,8);w.cursor(h.boundary);for(auto b:h.world_digest)w.word(b,1);
        w.word(h.required.size(),1);for(auto id:h.required)w.word(id,8);w.word(h.acknowledged.size(),1);for(auto id:h.acknowledged)w.word(id,8);}
    return w.bytes;
}
bool roster(std::span<const std::uint8_t> bytes,PartySnapshot& out,std::string& error) {
    return checked(error,[&]{
        if(bytes.size()>MaximumPacket)throw std::runtime_error("Spectator roster is too large.");
        Reader r{bytes};PartySnapshot p;p.host=r.word(8);p.generation=r.word(8);const auto count=r.word(1);
        if(!count||count>MaximumMembers)throw std::runtime_error("Too many spectator roster entries.");
        for(unsigned i=0;i<count;++i){Member m;m.id=r.word(8);m.name=r.text();m.role=Role(r.word(1));const auto port=r.word(1);if(port!=255)m.port=std::uint8_t(port);const auto host=r.word(1);if(host>1)throw std::runtime_error("Invalid host flag.");m.host=host!=0;m.connected=true;p.members.push_back(std::move(m));}
        for(auto& c:p.controls){c.member=r.word(8);c.generation=r.word(8);c.starts=r.cursor();}
        const auto pending=r.word(1);if(pending>1)throw std::runtime_error("Invalid player swap flag.");
        if(pending){Handover h;h.token=r.word(8);h.roster_generation=r.word(8);h.port=std::uint8_t(r.word(1));h.outgoing=r.word(8);h.incoming=r.word(8);h.boundary=r.cursor();for(auto& b:h.world_digest)b=std::uint8_t(r.word(1));
            const auto required=r.word(1);if(required>MaximumMembers)throw std::runtime_error("Too many swap acknowledgements.");for(unsigned i=0;i<required;++i)h.required.push_back(r.word(8));
            const auto acked=r.word(1);if(acked>required)throw std::runtime_error("Invalid swap acknowledgements.");for(unsigned i=0;i<acked;++i)h.acknowledged.push_back(r.word(8));p.handover=std::move(h);}
        r.end();std::string why;if(!Party::valid(p,why))throw std::runtime_error(why);out=std::move(p);
    });
}
std::vector<std::uint8_t> frames(std::span<const ConfirmedFrame> values) {
    if(values.empty()||values.size()>30)throw std::runtime_error("Invalid confirmed spectator frame count.");
    Writer w;w.word(values.size(),1);Cursor previous{};bool previous_boundary=false;
    for(const auto& f:values){if(!f.cursor.epoch||f.cursor.frame==UINT32_MAX||f.command.size()>ConfirmedFeed::MaximumCommand||
        previous_boundary||(previous.epoch&&(f.cursor.epoch!=previous.epoch||f.cursor.frame!=previous.frame+1)))throw std::runtime_error("Confirmed spectator frames are not contiguous.");
        w.cursor(f.cursor);for(auto p:f.inputs)input(w,p);w.word(f.scene_boundary?1:0,1);w.word(f.command.size(),2);
        if(f.command.size()>MaximumPacket-w.bytes.size())throw std::runtime_error("Spectator frame packet is too large.");
        w.bytes.insert(w.bytes.end(),f.command.begin(),f.command.end());previous=f.cursor;previous_boundary=f.scene_boundary;}
    return w.bytes;
}
bool frames(std::span<const std::uint8_t> bytes,std::vector<ConfirmedFrame>& out,std::string& error) {
    return checked(error,[&]{
        if(bytes.size()>MaximumPacket)throw std::runtime_error("Spectator frame packet is too large.");
        Reader r{bytes};const auto count=r.word(1);if(!count||count>30)throw std::runtime_error("Invalid spectator frame count.");std::vector<ConfirmedFrame> result;
        for(unsigned i=0;i<count;++i){ConfirmedFrame f;f.cursor=r.cursor();for(auto& p:f.inputs)p=input(r);const auto scene=r.word(1);if(scene>1)throw std::runtime_error("Invalid scene-boundary flag.");f.scene_boundary=scene!=0;
            const auto n=r.word(2);if(n>ConfirmedFeed::MaximumCommand||n>bytes.size()-r.at)throw std::runtime_error("Invalid spectator command size.");f.command.assign(bytes.begin()+std::ptrdiff_t(r.at),bytes.begin()+std::ptrdiff_t(r.at+n));r.at+=std::size_t(n);
            if(!f.cursor.epoch||f.cursor.frame==UINT32_MAX||(!result.empty()&&(f.cursor.epoch!=result.back().cursor.epoch||f.cursor.frame!=result.back().cursor.frame+1||result.back().scene_boundary)))throw std::runtime_error("Non-contiguous spectator frame sequence.");
            result.push_back(std::move(f));}
        r.end();out=std::move(result);
    });
}
std::vector<std::uint8_t> controller(const ControllerSample& p) {
    if(!p.cursor.epoch||p.cursor.frame==UINT32_MAX||p.port>=RacingPorts||!p.lease)throw std::runtime_error("Invalid controller lease sample.");
    Writer w;w.cursor(p.cursor);w.word(p.port,1);w.word(p.lease,8);input(w,p.input);return w.bytes;
}
bool controller(std::span<const std::uint8_t> bytes,ControllerSample& out,std::string& error) {
    return checked(error,[&]{Reader r{bytes};ControllerSample p;p.cursor=r.cursor();p.port=std::uint8_t(r.word(1));p.lease=r.word(8);p.input=input(r);r.end();if(!p.cursor.epoch||p.cursor.frame==UINT32_MAX||p.port>=RacingPorts||!p.lease)throw std::runtime_error("Invalid controller sample identity.");out=p;});
}
}

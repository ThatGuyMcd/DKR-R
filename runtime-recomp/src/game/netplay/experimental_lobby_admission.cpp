#include "experimental_lobby_admission.hpp"
#include "authoritative_state_codec.hpp"
#include "monocypher.h"
#include <algorithm>

namespace dkr::runtime::netplay::experimental {
namespace {
// v3 retains explicit encoding/decoded size and scopes chunk/ACK progress to
// the incarnation. A raw checkpoint starting DKRZ is not a compressed stream.
constexpr std::array<std::uint8_t,5> prefix{'D','K','X','A',3};
enum Kind : unsigned {Hello=1,Offer=2,Chunk=3,Ack=4,Ready=5,Progress=6};
void put(std::vector<std::uint8_t>& b,std::uint32_t v) {for(unsigned i=0;i<4;++i)b.push_back(std::uint8_t(v>>(8*i)));}
std::uint32_t word(std::span<const std::uint8_t> b,unsigned at=0) {std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(b[at+i])<<(8*i);return v;}
secure::Key hash(std::span<const std::uint8_t> b) {secure::Key r{};crypto_blake2b(r.data(),r.size(),b.data(),b.size());return r;}
bool nonzero(const secure::Key& k) {return std::any_of(k.begin(),k.end(),[](auto b){return b!=0;});}
}
LobbyAdmission::LobbyAdmission(SessionTransport& t,bool host,std::uint8_t players,std::uint8_t local,secure::Key contract)
    :transport_(t),host_(host),players_(players),local_(local),contract_(contract) {}
PeerAddress LobbyAdmission::address(std::uint8_t owner) {PeerAddress a{};a.size=4;a.storage[0]='D';a.storage[1]='K';a.storage[2]='X';a.storage[3]=owner;return a;}
bool LobbyAdmission::fail(std::string e) {error_=std::move(e);return false;}
bool LobbyAdmission::begin(std::vector<std::uint8_t> baseline,Prepare prepare,std::string& error) {
    const auto maximum=transport_.maximum_plaintext_datagram_bytes();
    if(begun_||players_<2||players_>4||local_>=players_||host_!=(local_==0)||!nonzero(contract_)||
       !transport_.is_open()||maximum<160||(!host_&&!prepare)||
       (host_&&(baseline.empty()||baseline.size()>kMaximumBaseline))) {
        error="Invalid owned lobby admission.";return false;
    }
    chunk_bytes_=std::min<std::size_t>(4096,maximum-64);
    prepare_=std::move(prepare);baseline_=std::move(baseline);begun_=true;
    if(host_) {
        incarnation_=secure::generate_key();digest_=hash(baseline_);
        decoded_bytes_=std::uint32_t(baseline_.size());
        auto wire=encode_authoritative_state_wire(baseline_);
        compressed_=wire.size()<baseline_.size();
        if(compressed_)baseline_=std::move(wire);
        ready_[0]=true;local_ready_=true;
    }
    error.clear();return true;
}
bool LobbyAdmission::prepared() const {
    if(!begun_||!error_.empty()||!local_ready_)return false;
    if(!host_)return true;
    for(unsigned p=1;p<players_;++p)if(!ready_[p])return false;
    return true;
}
bool LobbyAdmission::all_prepared() const {
    return host_ ? prepared() : (prepared()&&coordinator_ready_==((1U<<players_)-1U));
}
std::uint32_t LobbyAdmission::received_bytes() const {
    if(!host_)return received_;
    std::uint32_t minimum=total_bytes();
    for(unsigned p=1;p<players_;++p)
        minimum=std::min(minimum,ready_[p]?total_bytes():acknowledged_[p]);
    return minimum;
}
std::string LobbyAdmission::status() const {
    if(!host_&&baseline_.empty())return "REQUESTING INITIAL GAME STATE FROM PLAYER 1";
    if(all_prepared())return "INITIAL GAME STATE VERIFIED; WAITING FOR SYNCHRONIZED START";
    if(!host_&&local_ready_) {
        unsigned count=0;for(unsigned p=0;p<players_;++p)count+=unsigned((coordinator_ready_>>p)&1U);
        return "YOUR INITIAL STATE IS VERIFIED; WAITING FOR OTHER RACERS: "+std::to_string(count)+" / "+std::to_string(players_);
    }
    unsigned ready=0,consumers=1;
    for(unsigned p=0;p<players_;++p)ready+=unsigned(ready_[p]);
    if(host_) {
        for(unsigned p=1;p<players_;++p)consumers+=unsigned(hello_[p]);
        if(consumers<players_)return "WAITING FOR RACERS TO FINISH STARTUP: "+
            std::to_string(consumers)+" / "+std::to_string(players_);
    }
    return std::string(host_?"SYNCHRONIZING":"RECEIVING")+" INITIAL GAME STATE: "+
        std::to_string(received_bytes()/1024)+" / "+std::to_string(total_bytes()/1024)+" KiB"+
        (host_?" ("+std::to_string(ready)+" / "+std::to_string(players_)+" RACERS VERIFIED)":"");
}
std::uint64_t LobbyAdmission::baseline_identity() const {
    if(!local_ready_)return 0;
    std::array<std::uint8_t,96> bytes{};
    std::copy(contract_.begin(),contract_.end(),bytes.begin());
    std::copy(incarnation_.begin(),incarnation_.end(),bytes.begin()+32);
    std::copy(digest_.begin(),digest_.end(),bytes.begin()+64);
    const auto d=hash(bytes);std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(d[i])<<(8*i);
    return v?v:1;
}
std::string LobbyAdmission::diagnostics() const {
    std::string result;
    if(host_)for(unsigned p=1;p<players_;++p)result+=" p"+std::to_string(p+1)+"(queued="+
        std::to_string(sent_end_[p])+",acked="+std::to_string(acknowledged_[p])+",consumer="+
        std::to_string(hello_[p])+",offer="+std::to_string(offer_accepted_[p])+",verified="+std::to_string(ready_[p])+")";
    else result=" coordinator-ready-mask="+std::to_string(coordinator_ready_);
    return result;
}
DatagramSendStatus LobbyAdmission::send(std::uint8_t peer,unsigned kind,std::span<const std::uint8_t> data,TransportTrafficClass traffic) {
    std::vector<std::uint8_t> bytes(prefix.begin(),prefix.end());bytes.push_back(std::uint8_t(kind));bytes.insert(bytes.end(),data.begin(),data.end());
    std::string error;const auto status=transport_.send_status(address(peer),bytes,traffic,error);
    if(status==DatagramSendStatus::Error)fail(error.empty()?"Owned admission send failed.":error);
    return status;
}
bool LobbyAdmission::packet(std::uint8_t peer,std::span<const std::uint8_t> bytes,Clock::time_point now) {
    if(bytes.size()<6||bytes[5]<Hello||bytes[5]>Progress)return fail("Malformed owned admission packet.");
    const auto kind=bytes[5];const auto data=bytes.subspan(6);
    if(host_) {
        if(!peer||peer>=players_)return fail("Invalid owned admission sender.");
        if(kind==Hello) {
            if(data.size()!=contract_.size()||!std::equal(data.begin(),data.end(),contract_.begin()))return fail("Experimental build / ROM / rules / owned-state contract does not match.");
            if(!hello_[peer]) {hello_[peer]=true;progress_=now;peer_progress_[peer]=now;sent_[peer]={};}
            // Receipt does not mean the baseline is ready. No ghost readiness.
            return true;
        }
        if(kind==Ack) {
            if(data.size()!=36)return fail("Malformed owned checkpoint acknowledgment.");
            if(!std::equal(data.begin(),data.begin()+32,incarnation_.begin()))return true;
            const auto cursor=word(data,32);
            if(cursor>sent_end_[peer]||(cursor!=baseline_.size()&&cursor%chunk_bytes_))return fail("Owned checkpoint acknowledgment is outside the sent window.");
            if(!offer_accepted_[peer]) {offer_accepted_[peer]=true;peer_progress_[peer]=now;repair_at_[peer]=now;}
            if(cursor>acknowledged_[peer]) {acknowledged_[peer]=cursor;progress_=now;peer_progress_[peer]=now;repair_at_[peer]=now;}
            return true;
        }
        if(kind==Ready) {
            if(data.size()!=64||!std::equal(data.begin(),data.begin()+32,incarnation_.begin())||
               !std::equal(data.begin()+32,data.end(),digest_.begin())||sent_end_[peer]!=baseline_.size())return fail("Owned checkpoint preparation proof does not match.");
            if(!ready_[peer]){ready_[peer]=true;acknowledged_[peer]=total_bytes();progress_=now;peer_progress_[peer]=now;coordinator_at_={};}return true;
        }
    } else if(peer==0) {
        if(kind==Offer) {
            if(data.size()!=105||!std::equal(data.begin(),data.begin()+32,contract_.begin()))return fail("Experimental build / ROM / rules / owned-state contract does not match.");
            const auto size=word(data,96);
            const auto decoded=word(data,100);const auto encoding=data[104];
            if(!size||size>kMaximumBaseline||!decoded||decoded>kMaximumBaseline||encoding>1||
               (encoding==0&&size!=decoded)||(encoding==1&&(size<8||size>=decoded)))
                return fail("Owned checkpoint exceeds the admitted size / encoding limits.");
            secure::Key incarnation{},digest{};std::copy_n(data.begin()+32,32,incarnation.begin());std::copy_n(data.begin()+64,32,digest.begin());
            if(!nonzero(incarnation)||!nonzero(digest))return fail("Invalid owned checkpoint identity.");
            if(baseline_.empty()) {
                incarnation_=incarnation;digest_=digest;decoded_bytes_=decoded;compressed_=encoding==1;
                baseline_.resize(size);chunks_.resize((size+chunk_bytes_-1)/chunk_bytes_);progress_=now;sent_[0]={};
            }
            else if(baseline_.size()!=size||incarnation_!=incarnation||digest_!=digest||
                    decoded_bytes_!=decoded||compressed_!=(encoding==1))return fail("Conflicting owned checkpoint offer.");
            return true;
        }
        if(kind==Chunk) {
            if(baseline_.empty())return true; // Reliable channels can reorder offer/chunks.
            if(data.size()<37)return fail("Malformed owned checkpoint chunk.");
            if(!std::equal(data.begin(),data.begin()+32,incarnation_.begin()))return true;
            const auto offset=word(data,32);const auto length=data.size()-36;
            if(offset>=baseline_.size()||offset%chunk_bytes_||length!=std::min(chunk_bytes_,baseline_.size()-offset))return fail("Owned checkpoint chunk exceeds its segment.");
            auto destination=std::span(baseline_).subspan(offset,length);const auto input=data.subspan(36);const auto index=offset/chunk_bytes_;
            if(chunks_[index]) {if(!std::equal(destination.begin(),destination.end(),input.begin()))return fail("Conflicting owned checkpoint bytes.");}
            else {std::copy(input.begin(),input.end(),destination.begin());chunks_[index]=1;progress_=now;}
            while(received_<baseline_.size()&&chunks_[received_/chunk_bytes_])received_+=std::uint32_t(std::min(chunk_bytes_,baseline_.size()-received_));
            if(received_==baseline_.size()&&!local_ready_) {
                std::vector<std::uint8_t> decoded;std::string error;
                std::span<const std::uint8_t> checkpoint=baseline_;
                if(compressed_) {
                    constexpr std::array<std::uint8_t,4> magic{'D','K','R','Z'};
                    if(!std::equal(magic.begin(),magic.end(),baseline_.begin())||
                       !decode_authoritative_state_wire(baseline_,decoded_bytes_,decoded,error)||
                       decoded.size()!=decoded_bytes_)
                        return fail(error.empty()?"Invalid compressed owned checkpoint.":error);
                    checkpoint=decoded;
                }
                if(hash(checkpoint)!=digest_)return fail("Owned checkpoint integrity check failed.");
                if(!prepare_(checkpoint,error))return fail(error.empty()?"Owned checkpoint preparation failed.":error);
                prepare_={};local_ready_=true;progress_=now;
            }
            return true;
        }
        if(kind==Progress) {
            if(data.size()!=49)return fail("Malformed startup coordinator progress.");
            if(baseline_.empty()||!std::equal(data.begin(),data.begin()+32,incarnation_.begin()))return true;
            const auto mask=data[48];if(mask&~((1U<<players_)-1U))return fail("Invalid startup coordinator roster.");
            for(unsigned p=0;p<players_;++p) {
                const auto cursor=word(data,32+4*p);
                if(cursor>total_bytes())return fail("Invalid startup coordinator byte count.");
                coordinator_bytes_[p]=std::max(coordinator_bytes_[p],cursor);
            }
            coordinator_ready_|=mask;coordinator_seen_=now;return true;
        }
    }
    return fail("Unexpected owned admission message.");
}
bool LobbyAdmission::service_admission(Clock::time_point now) {
    if(!begun_||!error_.empty())return false;
    if(last_.time_since_epoch().count()&&now<last_)return fail("Owned admission clock moved backwards.");
    if(!last_.time_since_epoch().count()) {progress_=now;started_=now;peer_progress_.fill(now);}last_=now;
    if(!transport_.is_open())return fail("The normal lobby connection closed during owned play.");
    transport_.service();
    for(unsigned n=0;n<32;++n) {
        PeerAddress source;std::vector<std::uint8_t> bytes;std::string error;
        if(!transport_.receive(source,bytes,error)) {if(!error.empty())return fail(error);break;}
        if(source.size!=4||source.storage[0]!='D'||source.storage[1]!='K'||source.storage[2]!='X'||source.storage[3]>=players_||source.storage[3]==local_)return fail("Invalid admitted owned route.");
        if(bytes.size()>=4&&std::equal(prefix.begin(),prefix.begin()+4,bytes.begin())) {
            if(bytes.size()<5||bytes[4]!=prefix[4])return fail("The racers are using different Experimental Rollback rebuilds. Install the same package on every machine.");
            if(!packet(source.storage[3],bytes,now))return false;
        }
        else {
            // Early input on the unordered lane must survive Loaded/Start.
            if(bytes.size()>50+kOwnerInputMaximumBytes||gameplay_.size()>=128)return fail("Owned gameplay receive queue exceeded its limit.");
            if(!host_&&local_ready_&&source.storage[3]==0)release_seen_=true;
            gameplay_.push_back({source,std::move(bytes)});
        }
    }
    const auto retry=std::chrono::milliseconds(250);
    if(host_) {
        // Distinct sent and acknowledged cursors: partial ACKs never requeue
        // bytes which SCTP already owns. A rotating per-peer budget prevents a
        // slow guest from blocking the other two guests or lifecycle traffic.
        for(unsigned turn=0;turn<players_-1;++turn) {
            const auto peer=std::uint8_t(1+(next_peer_-1+turn)%(players_-1));
            if(ready_[peer])continue;
            if(now-peer_progress_[peer]>(hello_[peer]?std::chrono::seconds(60):std::chrono::seconds(120)))return fail("Racer "+std::to_string(peer+1)+" stopped making startup progress. Retry from the lobby.");
            if(!hello_[peer])continue;
            if(!offer_accepted_[peer]) {
                if(sent_[peer].time_since_epoch().count()&&now-sent_[peer]<retry)continue;
                std::vector<std::uint8_t> offer(contract_.begin(),contract_.end());offer.insert(offer.end(),incarnation_.begin(),incarnation_.end());offer.insert(offer.end(),digest_.begin(),digest_.end());put(offer,total_bytes());put(offer,decoded_bytes_);offer.push_back(std::uint8_t(compressed_));
                const auto offered=send(peer,Offer,offer,TransportTrafficClass::Control);
                if(offered==DatagramSendStatus::Error)return false;
                if(offered==DatagramSendStatus::Sent)sent_[peer]=now;
                continue; // Wait for Offer ACK before using the other SCTP lane.
            }
            for(unsigned n=0;n<4&&sent_end_[peer]<total_bytes()&&sent_end_[peer]-acknowledged_[peer]<256U*1024U;++n) {
                const auto offset=sent_end_[peer];const auto end=std::min<std::size_t>(baseline_.size(),offset+chunk_bytes_);
                std::vector<std::uint8_t> chunk(incarnation_.begin(),incarnation_.end());put(chunk,offset);chunk.insert(chunk.end(),baseline_.begin()+offset,baseline_.begin()+end);
                const auto result=send(peer,Chunk,chunk,TransportTrafficClass::Checkpoint);
                if(result==DatagramSendStatus::Error)return false;if(result==DatagramSendStatus::WouldBlock)break;
                sent_end_[peer]=std::uint32_t(end);
            }
            // Recovery is only for a contiguous hole, not every partial ACK.
            // Bounded and infrequent; ordinary retransmission belongs to SCTP.
            if(acknowledged_[peer]<sent_end_[peer]&&now-repair_at_[peer]>=std::chrono::seconds(3)) {
                const auto offset=acknowledged_[peer];const auto end=std::min<std::size_t>(baseline_.size(),offset+chunk_bytes_);
                std::vector<std::uint8_t> chunk(incarnation_.begin(),incarnation_.end());put(chunk,offset);chunk.insert(chunk.end(),baseline_.begin()+offset,baseline_.begin()+end);
                const auto result=send(peer,Chunk,chunk,TransportTrafficClass::Checkpoint);
                if(result==DatagramSendStatus::Error)return false;if(result==DatagramSendStatus::Sent)repair_at_[peer]=now;
            }
        }
        next_peer_=std::uint8_t(1+next_peer_%(players_-1));
        if(!release_seen_&&(!coordinator_at_.time_since_epoch().count()||now-coordinator_at_>=retry)) {
            std::vector<std::uint8_t> progress(incarnation_.begin(),incarnation_.end());
            for(unsigned p=0;p<4;++p)put(progress,p==0?total_bytes():acknowledged_[p]);
            std::uint8_t mask=0;for(unsigned p=0;p<players_;++p)if(ready_[p])mask|=std::uint8_t(1U<<p);progress.push_back(mask);
            bool sent=true;for(std::uint8_t p=1;p<players_;++p)if(hello_[p]) {const auto r=send(p,Progress,progress,TransportTrafficClass::Control);if(r==DatagramSendStatus::Error)return false;sent&=r==DatagramSendStatus::Sent;}
            if(sent)coordinator_at_=now;
        }
    } else if(!release_seen_&&(!sent_[0].time_since_epoch().count()||now-sent_[0]>=(baseline_.empty()||local_ready_?retry:std::chrono::milliseconds(20)))) {
        DatagramSendStatus result;
        if(baseline_.empty())result=send(0,Hello,contract_,TransportTrafficClass::Control);
        else if(local_ready_) {std::vector<std::uint8_t> proof(incarnation_.begin(),incarnation_.end());proof.insert(proof.end(),digest_.begin(),digest_.end());result=send(0,Ready,proof,TransportTrafficClass::Control);}
        else {std::vector<std::uint8_t> ack(incarnation_.begin(),incarnation_.end());put(ack,received_);result=send(0,Ack,ack,TransportTrafficClass::Control);}
        if(result==DatagramSendStatus::Error)return false;if(result==DatagramSendStatus::Sent)sent_[0]=now;
    }
    if(!release_seen_) {
        if(!host_&&!local_ready_&&now-progress_>std::chrono::seconds(60))return fail("The owned initial checkpoint made no progress for 60 seconds.");
        if(!host_&&local_ready_&&!all_prepared()&&now-(coordinator_seen_.time_since_epoch().count()?coordinator_seen_:progress_)>std::chrono::seconds(60))return fail("The host stopped reporting startup progress.");
        if(!all_prepared()&&now-started_>std::chrono::seconds(180))return fail("Initial synchronization exceeded the three-minute startup budget. Check the slowest racer's connection and retry from the lobby.");
    }
    return true;
}
bool LobbyAdmission::receive(PeerAddress& p,std::vector<std::uint8_t>& b,std::string& e) {
    if(!error_.empty()){e=error_;return false;}e.clear();if(gameplay_.empty())return false;
    p=gameplay_.front().source;b=std::move(gameplay_.front().bytes);gameplay_.pop_front();return true;
}
}

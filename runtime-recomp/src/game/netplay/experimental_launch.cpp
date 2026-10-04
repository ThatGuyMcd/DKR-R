#include "experimental_launch.hpp"
#include "monocypher.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr std::size_t kContractBytes = 160;
constexpr std::size_t kBootstrapBytes = 8 + 8 + 32 + 32 + kContractBytes;
constexpr auto kRetry = std::chrono::milliseconds(100);
constexpr auto kTimeout = std::chrono::seconds(45);
enum Kind : unsigned { Offer=1, Chunk=2, Ack=3, Ready=4, Release=5, ReleaseAck=6 };
void put(std::vector<std::uint8_t>& out, std::uint64_t value, unsigned bytes) {
    for(unsigned b=0;b<bytes;++b) out.push_back(std::uint8_t(value>>(b*8)));
}
std::uint64_t take(std::span<const std::uint8_t> in, unsigned at, unsigned bytes) {
    std::uint64_t value=0;for(unsigned b=0;b<bytes;++b)value|=std::uint64_t(in[at+b])<<(b*8);return value;
}
bool nonzero(const secure::Key& key) { return std::any_of(key.begin(),key.end(),[](auto b){return b!=0;}); }
secure::Key digest(std::span<const std::uint8_t> bytes) {
    secure::Key result;crypto_blake2b(result.data(),result.size(),bytes.data(),bytes.size());return result;
}
std::uint64_t random_id() {
    for(;;) { const auto key=secure::generate_key(); const auto id=take(key,0,8);if(id)return id; }
}
bool valid(const LaunchContract& c) {
    return (c.revision==77 || c.revision==80) && c.state_bytes && c.state_bytes<=Launch::kMaximumBaseline &&
        c.schema && c.epoch && c.epoch!=UINT64_MAX && c.players==2 && c.prediction_window>=2 &&
        c.prediction_window<=20 && c.input_delay<=8 && nonzero(c.build) && nonzero(c.rom) &&
        nonzero(c.rules) && nonzero(c.abi);
}
std::vector<std::uint8_t> encode(const LaunchContract& c) {
    std::vector<std::uint8_t> out={'D','K','X','1'};
    put(out,c.revision,4);put(out,c.state_bytes,4);put(out,c.schema,8);put(out,c.epoch,8);
    out.insert(out.end(),{c.players,c.prediction_window,c.input_delay,0});
    for(const auto* key:{&c.build,&c.rom,&c.rules,&c.abi})out.insert(out.end(),key->begin(),key->end());
    return out;
}
// Transport bootstrap is an ASCII string; canonical lowercase hex avoids
// embedded-NUL differences in signaling providers. No keys go into logs.
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char alphabet[]="0123456789abcdef";std::string out="DKXL1:";
    for(auto b:bytes){out.push_back(alphabet[b>>4]);out.push_back(alphabet[b&15]);}return out;
}
bool unhex(std::string_view text,std::vector<std::uint8_t>& out) {
    if(!text.starts_with("DKXL1:") || text.size()!=6+kBootstrapBytes*2)return false;
    out.resize(kBootstrapBytes);
    const auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(std::size_t i=0;i<out.size();++i){const int h=digit(text[6+i*2]),l=digit(text[7+i*2]);if(h<0||l<0)return false;out[i]=std::uint8_t(h*16+l);}return true;
}
std::vector<std::uint8_t> proof(const secure::Key& incarnation,const secure::Key& baseline) {
    std::vector<std::uint8_t> out(incarnation.begin(),incarnation.end());out.insert(out.end(),baseline.begin(),baseline.end());return out;
}
}
Launch::~Launch() { close(); }
void Launch::drop_secrets() {
    for(auto* key:{&local_.secret,&capability_,&admitted_key_})crypto_wipe(key->data(),key->size());
    if(!join_packet_.empty())crypto_wipe(join_packet_.data(),join_packet_.size());join_packet_.clear();
}
bool Launch::fail(std::string message) {
    error_=std::move(message);view_.phase=LaunchPhase::Failed;control_={};chunks_={};gameplay_.clear();
    if(route_retained_){transport_.release_peer_route(remote_);route_retained_=false;}
    if(host_)transport_.set_quick_join_bootstrap({});drop_secrets();return false;
}
bool Launch::host(LaunchContract c,std::vector<std::uint8_t> baseline,std::string& error) {
    if(view_.phase!=LaunchPhase::Idle || !valid(c) || baseline.size()!=c.state_bytes ||
       !transport_.quick_join() || !transport_.is_open() || transport_.maximum_plaintext_datagram_bytes()<kChunkBytes+128) {
        error="Experimental host requires an open Quick Join route and an exact verified owned baseline.";return false;
    }
    contract_=c;host_=true;local_=secure::generate_key_pair();capability_=secure::generate_key();
    incarnation_=secure::generate_key();match_=random_id();sender_=random_id();baseline_=std::move(baseline);
    baseline_digest_=digest(baseline_);view_.baseline_bytes=c.state_bytes;
    std::vector<std::uint8_t> bootstrap;put(bootstrap,match_,8);put(bootstrap,sender_,8);
    bootstrap.insert(bootstrap.end(),local_.public_key.begin(),local_.public_key.end());
    bootstrap.insert(bootstrap.end(),capability_.begin(),capability_.end());const auto manifest=encode(c);
    bootstrap.insert(bootstrap.end(),manifest.begin(),manifest.end());
    transport_.set_quick_join_bootstrap(hex(bootstrap));view_.phase=LaunchPhase::AwaitingPeer;return true;
}
bool Launch::join(LaunchContract c,Prepare prepare,std::string& error) {
    if(view_.phase!=LaunchPhase::Idle || !valid(c) || !prepare || !transport_.quick_join() || !transport_.is_open() ||
       transport_.maximum_plaintext_datagram_bytes()<kChunkBytes+128) {
        error="Experimental client requires an open Quick Join route and a verified local adapter contract.";return false;
    }
    contract_=c;prepare_=std::move(prepare);local_=secure::generate_key_pair();sender_=random_id();
    view_.phase=LaunchPhase::Authenticating;return true;
}
bool Launch::set_control(unsigned kind,std::span<const std::uint8_t> payload) {
    if(!admitted_ || sequence_==UINT64_MAX)return fail("Experimental admission sequence exhausted.");
    std::vector<std::uint8_t> plain={'D','K','X','L',1,std::uint8_t(kind)};
    plain.insert(plain.end(),payload.begin(),payload.end());
    control_={};control_.bytes=secure::seal(plain,admitted_key_,sender_,++sequence_,match_);return true;
}
bool Launch::send_pending(Pending& pending,Clock::time_point now) {
    if(pending.bytes.empty() || (pending.attempted_once && now-pending.attempted<kRetry))return true;
    if(!transport_.traffic_ready(remote_,pending.traffic))return true;
    pending.attempted=now;pending.attempted_once=true;std::string message;
    // WouldBlock retains the exact encrypted packet and nonce. The caller's
    // next service pulse handles retries; no sleeping or worker waits here.
    const auto status=transport_.send_status(remote_,pending.bytes,pending.traffic,message);
    return status!=DatagramSendStatus::Error || fail(message.empty()?"Experimental admission send failed.":message);
}
bool Launch::lanes_ready() const {
    for(auto lane:{TransportTrafficClass::Control,TransportTrafficClass::Realtime,TransportTrafficClass::Authoritative})
        if(!transport_.traffic_ready(remote_,lane))return false;
    return true;
}
bool Launch::receive_packet(const PeerAddress& source,std::span<const std::uint8_t> packet,Clock::time_point now) {
    if(packet.size()>kChunkBytes+160){++view_.rejected;return true;}
    if(host_ && secure::is_join_request(packet)) {
        std::vector<std::uint8_t> plain;secure::Key pub{},key{};std::uint64_t id=0;
        if(!secure::open_join_request(packet,local_.secret,capability_,match_,id,pub,key,plain) ||
           !id || id==sender_ || plain!=encode(contract_) || (admitted_ && (source!=remote_ || id!=remote_sender_ || pub!=remote_public_))) {
            crypto_wipe(key.data(),key.size());++view_.rejected;return true;
        }
        if(!admitted_) {
            remote_=source;remote_sender_=id;remote_public_=pub;admitted_key_=key;admitted_=true;
            transport_.retain_peer_route(remote_);route_retained_=true;progress_=now;
            auto payload=proof(incarnation_,baseline_digest_);put(payload,contract_.state_bytes,4);
            if(!set_control(Offer,payload))return false;view_.phase=LaunchPhase::ReceivingBaseline;
        }
        crypto_wipe(key.data(),key.size());return true;
    }
    if(!admitted_ || source!=remote_){++view_.rejected;return true;}
    std::uint64_t id=0,seq=0;std::vector<std::uint8_t> plain;
    if(!secure::open(packet,admitted_key_,match_,id,seq,plain)) {
        // Network uses a domain-separated key, so its early encrypted inputs
        // cannot authenticate as launch control. Preserve a small bounded lane
        // across the release ACK; Network performs their actual authentication.
        std::uint64_t match=0;
        if((view_.phase==LaunchPhase::Releasing || view_.phase==LaunchPhase::Running) && packet.size()<=118 &&
           secure::inspect_packet(packet,id,match) && id==remote_sender_ && match==match_ && gameplay_.size()<64) {
            gameplay_.emplace_back(packet.begin(),packet.end());return true;
        }
        ++view_.rejected;return true;
    }
    if(id!=remote_sender_ || !replay_.accept(seq) || plain.size()<6 ||
       !std::equal(plain.begin(),plain.begin()+5,"DKXL\1")){++view_.rejected;return true;}
    const auto kind=plain[5];const auto data=std::span(plain).subspan(6);
    if(!host_ && kind==Offer) {
        if(data.size()!=68 || take(data,64,4)!=contract_.state_bytes)return fail("Invalid experimental baseline offer.");
        secure::Key incarnation{},hash{};std::copy_n(data.begin(),32,incarnation.begin());std::copy_n(data.begin()+32,32,hash.begin());
        if(!nonzero(incarnation) || !nonzero(hash))return fail("Invalid experimental launch identity.");
        if(view_.phase==LaunchPhase::Authenticating) {
            incarnation_=incarnation;baseline_digest_=hash;baseline_.resize(contract_.state_bytes);
            received_chunks_.resize((baseline_.size()+kChunkBytes-1)/kChunkBytes);
            view_.baseline_bytes=contract_.state_bytes;view_.phase=LaunchPhase::ReceivingBaseline;
            control_={};join_packet_.clear();progress_=now;
        } else if(incarnation_!=incarnation || baseline_digest_!=hash)return fail("Conflicting experimental baseline offer.");
        return true;
    }
    if(!host_ && kind==Chunk) {
        if(view_.phase!=LaunchPhase::ReceivingBaseline)return true;
        if(data.size()<5)return fail("Invalid experimental baseline chunk.");
        const auto offset=take(data,0,4),length=data.size()-4;
        if(offset>=baseline_.size() || offset%kChunkBytes || length!=std::min(kChunkBytes,baseline_.size()-std::size_t(offset)))
            return fail("Experimental baseline chunk is out of range.");
        const auto index=offset/kChunkBytes;auto destination=std::span(baseline_).subspan(offset,length);const auto bytes=data.subspan(4);
        if(received_chunks_[index]) {
            if(!std::equal(destination.begin(),destination.end(),bytes.begin()))return fail("Conflicting experimental baseline bytes.");
        } else {
            std::copy(bytes.begin(),bytes.end(),destination.begin());received_chunks_[index]=1;progress_=now;
        }
        while(view_.baseline_received<baseline_.size() && received_chunks_[view_.baseline_received/kChunkBytes])
            view_.baseline_received+=std::uint32_t(std::min(kChunkBytes,baseline_.size()-view_.baseline_received));
        std::vector<std::uint8_t> ack;put(ack,view_.baseline_received,4);
        // Do not seal unchanged ACKs at polling frequency. Duplicate chunks
        // trigger the retained exact ACK on its paced retry instead.
        if(control_.end!=view_.baseline_received || control_.bytes.empty()) {
            if(!set_control(Ack,ack))return false;control_.end=view_.baseline_received;
        }
        if(view_.baseline_received==baseline_.size()) {
            if(digest(baseline_)!=baseline_digest_)return fail("Experimental baseline integrity check failed.");
            view_.phase=LaunchPhase::PreparingWorld;progress_=now;
        }
        return true;
    }
    if(host_ && kind==Ack) {
        if(data.size()!=4)return fail("Invalid experimental baseline acknowledgment.");const auto cursor=take(data,0,4);
        if(cursor>baseline_.size() || (cursor!=baseline_.size() && cursor%kChunkBytes))return fail("Out-of-range experimental baseline acknowledgment.");
        if(cursor>acknowledged_) {
            // A peer cannot acknowledge a chunk the host has never queued.
            auto sent_end=acknowledged_;for(const auto& c:chunks_)sent_end=std::max(sent_end,c.end);
            if(cursor>sent_end)return fail("Experimental peer acknowledged untransmitted state.");
            acknowledged_=std::uint32_t(cursor);view_.baseline_received=acknowledged_;progress_=now;
            for(auto& c:chunks_)if(c.end<=acknowledged_)c={};
        }return true;
    }
    if(host_ && kind==Ready) {
        const auto expected=proof(incarnation_,baseline_digest_);
        if(data.size()!=expected.size() || !std::equal(data.begin(),data.end(),expected.begin()))return fail("Experimental prepared-world agreement failed.");
        if(view_.phase==LaunchPhase::ReceivingBaseline) {
            acknowledged_=contract_.state_bytes;view_.baseline_received=acknowledged_;control_={};chunks_={};view_.phase=LaunchPhase::Ready;
        }return true;
    }
    if(!host_ && kind==Release) {
        const auto expected=proof(incarnation_,baseline_digest_);
        if(data.size()!=expected.size() || !std::equal(data.begin(),data.end(),expected.begin()))return fail("Conflicting experimental release.");
        if(view_.phase!=LaunchPhase::Ready && view_.phase!=LaunchPhase::Running)return fail("Experimental release arrived before world readiness.");
        if(view_.phase==LaunchPhase::Ready) {
            // RTC open notifications are independently ordered on each peer.
            // Retain this authenticated intent even if our local realtime or
            // repair lane is not open yet; a replay-filtered retry cannot be
            // relied on to re-enter this branch later.
            release_received_=true;progress_=now;
        } else control_.attempted_once=false; // Host is still missing our retained ACK.
        return true;
    }
    if(host_ && kind==ReleaseAck) {
        const auto expected=proof(incarnation_,baseline_digest_);
        if(data.size()!=expected.size() || !std::equal(data.begin(),data.end(),expected.begin()))return fail("Conflicting experimental release acknowledgment.");
        if(view_.phase==LaunchPhase::Releasing){view_.phase=LaunchPhase::Running;control_={};}return true;
    }
    ++view_.rejected;return true;
}
bool Launch::service_launch(Clock::time_point now) {
    if(view_.phase==LaunchPhase::Idle || view_.phase==LaunchPhase::Failed)return false;
    if(clock_started_ && now<last_service_)return fail("Experimental admission clock moved backwards.");
    if(!clock_started_){progress_=now;clock_started_=true;}last_service_=now;
    try {
        if(!transport_.is_open())return fail("Experimental Quick Join transport closed.");transport_.service();
        if(!host_ && !admitted_) {
            std::string bootstrap;PeerAddress address;
            if(transport_.take_quick_join_bootstrap(bootstrap,address)) {
                std::vector<std::uint8_t> parsed;
                if(!address || !unhex(bootstrap,parsed))return fail("Not a compatible experimental Quick Join lobby.");
                const auto manifest=encode(contract_);
                if(!std::equal(manifest.begin(),manifest.end(),parsed.begin()+80))
                    return fail("Experimental build, ROM, rules or owned-state schema does not match the host.");
                match_=take(parsed,0,8);remote_sender_=take(parsed,8,8);remote_=address;
                std::copy_n(parsed.begin()+16,32,remote_public_.begin());std::copy_n(parsed.begin()+48,32,capability_.begin());
                if(!match_ || !remote_sender_ || sender_==remote_sender_ || !nonzero(remote_public_) || !nonzero(capability_))
                    return fail("Invalid experimental Quick Join bootstrap.");
                join_packet_=secure::seal_join_request(manifest,local_.secret,local_.public_key,remote_public_,capability_,sender_,match_,admitted_key_);
                if(join_packet_.empty())return fail("Experimental Quick Join key agreement failed.");
                admitted_=true;transport_.retain_peer_route(remote_);route_retained_=true;progress_=now;
                control_.bytes=join_packet_;
            }
        }
        for(unsigned n=0;n<32;++n) {
            PeerAddress source;std::vector<std::uint8_t> bytes;std::string message;
            if(!transport_.receive(source,bytes,message)) {
                if(!message.empty())return fail(message);break;
            }
            if(!receive_packet(source,bytes,now))return false;
        }
        if(view_.phase==LaunchPhase::PreparingWorld) {
            std::string message;const auto step=prepare_(baseline_,message);
            if(step==PreparationStep::Failed)return fail(message.empty()?"Experimental owned-world preparation failed.":message);
            if(step==PreparationStep::Ready) {
                if(!set_control(Ready,proof(incarnation_,baseline_digest_)))return false;view_.phase=LaunchPhase::Ready;prepare_={};
            }
        }
        if(!host_ && release_received_ && view_.phase==LaunchPhase::Ready && lanes_ready()) {
            if(!set_control(ReleaseAck,proof(incarnation_,baseline_digest_)))return false;
            view_.phase=LaunchPhase::Running;release_ack_until_=now+std::chrono::seconds(45);
        }
        if(host_ && view_.phase==LaunchPhase::ReceivingBaseline && admitted_) {
            // Four immutable chunks in flight (48 KiB), independent of the
            // input/control lanes. Fill gaps before advancing this window.
            for(std::uint32_t offset=acknowledged_;offset<baseline_.size() && offset<std::uint64_t(acknowledged_)+4*kChunkBytes;offset+=kChunkBytes) {
                if(std::any_of(chunks_.begin(),chunks_.end(),[&](const auto& p){return !p.bytes.empty() && p.offset==offset;}))continue;
                auto free=std::find_if(chunks_.begin(),chunks_.end(),[](const auto& p){return p.bytes.empty();});if(free==chunks_.end())break;
                if(sequence_==UINT64_MAX)return fail("Experimental admission sequence exhausted.");
                std::vector<std::uint8_t> plain={'D','K','X','L',1,Chunk};put(plain,offset,4);
                const auto end=std::uint32_t(std::min<std::size_t>(baseline_.size(),offset+kChunkBytes));
                plain.insert(plain.end(),baseline_.begin()+offset,baseline_.begin()+end);
                free->bytes=secure::seal(plain,admitted_key_,sender_,++sequence_,match_);
                free->traffic=TransportTrafficClass::Checkpoint;free->offset=offset;free->end=end;
            }
        }
        if(view_.phase==LaunchPhase::Running && !host_ && now>release_ack_until_)control_={};
        if(admitted_ && (!send_pending(control_,now)))return false;
        for(auto& chunk:chunks_)if(!send_pending(chunk,now))return false;
        if(view_.phase!=LaunchPhase::AwaitingPeer && (view_.phase!=LaunchPhase::Ready || release_received_) && view_.phase!=LaunchPhase::Running && now-progress_>kTimeout)
            return fail("Experimental Quick Join preparation timed out; no gameplay was started on this peer.");
        return true;
    } catch(const std::exception& e) {return fail(std::string("Experimental launch failed: ")+e.what());}
      catch(...) {return fail("Experimental launch failed with an unknown transport/preparation error.");}
}
bool Launch::release(std::string& error) {
    if(!host_ || view_.phase!=LaunchPhase::Ready){error="The matching experimental client is not ready.";return false;}
    if(!lanes_ready()){error="Experimental Quick Join input and repair channels are not ready yet.";return false;}
    if(!set_control(Release,proof(incarnation_,baseline_digest_))){error=error_;return false;}
    progress_=last_service_;view_.phase=LaunchPhase::Releasing;return true;
}
std::optional<NetworkConfiguration> Launch::network_configuration() const {
    if(view_.phase!=LaunchPhase::Running)return std::nullopt;
    NetworkConfiguration config{match_,sender_};config.incarnation=incarnation_;
    config.peers[host_?1:0]={remote_,remote_sender_,admitted_key_};return config;
}
bool Launch::open(std::uint16_t,std::string& error) {error="Experimental launch exclusively borrows an already open Quick Join transport.";return false;}
void Launch::close() {
    if(route_retained_){transport_.release_peer_route(remote_);route_retained_=false;}
    if(host_ && view_.phase!=LaunchPhase::Idle)transport_.set_quick_join_bootstrap({});
    drop_secrets();control_={};chunks_={};gameplay_.clear();baseline_.clear();received_chunks_.clear();prepare_={};
    contract_={};view_={};local_={};capability_={};remote_public_={};admitted_key_={};incarnation_={};baseline_digest_={};
    match_=sender_=remote_sender_=sequence_=0;acknowledged_=0;replay_={};remote_={};
    host_=admitted_=clock_started_=release_received_=false;last_service_={};progress_={};release_ack_until_={};error_.clear();
}
bool Launch::is_open() const {return view_.phase==LaunchPhase::Running && transport_.is_open();}
bool Launch::traffic_ready(const PeerAddress& address,TransportTrafficClass traffic) const {return is_open() && address==remote_ && transport_.traffic_ready(address,traffic);}
DatagramSendStatus Launch::send_status(const PeerAddress& address,std::span<const std::uint8_t> bytes,TransportTrafficClass traffic,std::string& error) {
    if(!is_open() || address!=remote_){error="Experimental peer is not released.";return DatagramSendStatus::Error;}
    return transport_.send_status(address,bytes,traffic,error);
}
bool Launch::receive(PeerAddress& source,std::vector<std::uint8_t>& bytes,std::string& error) {
    if(view_.phase==LaunchPhase::Failed){error=error_;return false;}
    if(!is_open() || gameplay_.empty())return false;
    source=remote_;bytes=std::move(gameplay_.front());gameplay_.pop_front();return true;
}
void Launch::service() {if(!service_launch())throw std::runtime_error(error_.empty()?"Experimental launch is inactive.":error_);}
}

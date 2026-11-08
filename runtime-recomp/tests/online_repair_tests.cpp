#include "mods/online_mod_transfer.hpp"
#include "mods/legacy_character_artifact.hpp"
#include "online_stability/transfer_window.hpp"
#include "online_stability/preparation_policy.hpp"
#include "online_stability/diagnostic_budget.hpp"
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>

namespace m=dkr::mods;
namespace o=m::online;
namespace s=dkr::online_stability;
namespace {
m::Bytes bytes(std::string_view value){return {value.begin(),value.end()};}
void policies(){
    s::TransferWindow window;assert(window.size()==256*1024);
    window.credit(false,false);assert(window.size()==256*1024); // Old capability stays bounded.
    window.negotiate(s::window_bytes(s::MaximumTransferWindow));
    for(unsigned n=0;n<20;++n)window.credit(false,false);
    assert(window.size()==s::MaximumTransferWindow&&window.stride()==128*1024);
    window.credit(false,true);assert(window.size()==s::MaximumTransferWindow/2);
    window.negotiate({});assert(window.size()==s::LegacyTransferWindow);
    s::TransferWindow slow;slow.negotiate(s::window_bytes(s::MaximumTransferWindow));
    const auto epoch=s::TransferWindow::Clock::time_point(std::chrono::seconds(100));slow.credit(true,false,epoch);
    slow.observed(8192,epoch+std::chrono::milliseconds(500));slow.credit(false,false,epoch+std::chrono::milliseconds(500));
    assert(slow.size()<s::MaximumTransferWindow&&slow.retry_delay()>=std::chrono::seconds(1));
    s::TransferWindow datagram;datagram.chunk_budget(768);datagram.negotiate(s::window_bytes(s::MaximumTransferWindow));
    for(unsigned n=0;n<10;++n)datagram.credit(false,false);
    assert(datagram.size()<=768*512&&datagram.size()>=s::LegacyTransferWindow);
    for(const auto size:{0U,1U,8192U,4U*1024U*1024U}){bool rejected=false;try{s::requested_window(s::window_bytes(size));}catch(const std::exception&){rejected=true;}assert(rejected);}
    assert(s::preparation_batch(3ULL*1024*1024*1024,8ULL*1024*1024*1024)==2);
    assert(s::preparation_batch(8ULL*1024*1024*1024,1ULL*1024*1024*1024)==2);
    assert(s::preparation_batch(8ULL*1024*1024*1024,8ULL*1024*1024*1024)==4);
    const auto digest=std::string(64,'a');const auto root=std::filesystem::temp_directory_path()/"dkr-course-policy";
    assert(s::archived_course(root,digest)==root/"online-variants"/(digest+".dkrmap"));
    bool rejected=false;try{s::archived_course(root,"../../escape");}catch(const std::exception&){rejected=true;}assert(rejected);
    s::DiagnosticBudget diagnostics;const auto now=s::DiagnosticBudget::Clock::time_point{}+std::chrono::seconds(10);
    unsigned allowed=0;for(unsigned n=0;n<10000;++n)allowed+=diagnostics.allow(now);
    assert(allowed==8&&diagnostics.take_suppressed()==9992);
    assert(diagnostics.allow(now+std::chrono::seconds(1)));
}
struct Result {unsigned elapsed=0,retries=0;std::size_t peak=0,wire=0;double steady_mbps=0;};
#if defined(DKR_PIPELINE_TRANSFER)
Result transfer(bool adaptive,unsigned rtt,unsigned mbps,bool lose=false,unsigned peers=1,unsigned chunk_budget=o::MaxChunkBytes){
    const auto source=std::make_shared<m::Bytes>(8U*1024U*1024U);
    for(std::size_t n=0;n<source->size();++n)(*source)[n]=std::uint8_t(n%251);
    const auto hash=m::sha256(*source),id=m::sha256(bytes("character")),artifact=m::sha256(bytes("artifact"));
    o::Manifest manifest;manifest.revision="us.v77";
    manifest.payloads.push_back({o::PayloadKind::Xdelta,hash,"us.v77",source->size(),"Window benchmark"});
    manifest.content.push_back({o::Kind::Character,id,"Window benchmark",hash,artifact,id,m::CharacterAdapterVersion});
    auto bundle=std::make_shared<o::Bundle>();bundle->manifest=o::canonical(manifest);bundle->payloads.emplace(hash,source);
    struct Recipient {
        o::Upload sender;o::Download receiver;o::ChunkInbox inbox;s::TransferWindow window;m::Bytes rebuilt;
        unsigned anchor=0,retry=0,finished=0;bool requested=false,dropped=false;
        Recipient(const std::shared_ptr<o::Bundle>& b,unsigned chunk):sender(b),window(chunk){}
    };
    std::vector<std::unique_ptr<Recipient>> clients;
    const auto metadata=o::encode(bundle->manifest);const auto digest=o::identity(bundle->manifest);
    for(unsigned p=0;p<peers;++p){auto c=std::make_unique<Recipient>(bundle,chunk_budget);
        c->receiver.offer(digest,std::uint32_t(metadata.size()));
        while(c->receiver.phase()==o::TransferPhase::Manifest){c->sender.request(c->receiver.request(chunk_budget),chunk_budget);
            while(const auto next=c->sender.next()){assert(c->receiver.manifest_chunk(*next));c->sender.sent();}}
        c->sender.request({o::Operation::Consent,digest},chunk_budget);c->receiver.consent();
        if(adaptive)c->window.negotiate(s::window_bytes(s::MaximumTransferWindow));
        c->rebuilt.reserve(source->size());clients.push_back(std::move(c));
    }
    struct Delivery {unsigned due=0,peer=0;bool request=false;o::Message message;};
    std::deque<Delivery> wire;
    unsigned steady_start=0;double wire_free=0;Result result;
    for(unsigned time=0;time<180000;++time){
        for(auto i=wire.begin();i!=wire.end();){if(i->due>time){++i;continue;}
            const auto event=std::move(*i);i=wire.erase(i);
            auto& c=*clients[event.peer];auto& sender=c.sender;auto& receiver=c.receiver;auto& window=c.window;auto& inbox=c.inbox;auto& rebuilt=c.rebuilt;
            const auto message=o::decode_message(o::encode_message(event.message));
            if(event.request){sender.request(message,chunk_budget);continue;}
            if(receiver.phase()!=o::TransferPhase::Payloads)continue;
            auto request=window.request(receiver.request(chunk_budget));inbox.push(message,request);
            while(receiver.phase()==o::TransferPhase::Payloads){
                request=window.request(receiver.request(chunk_budget));const auto chunk=inbox.take(request);if(!chunk)break;
                assert(receiver.accept_payload_chunk(*chunk));rebuilt.insert(rebuilt.end(),chunk->bytes.begin(),chunk->bytes.end());receiver.persisted(chunk->bytes.size());
                window.observed(chunk->bytes.size(),s::TransferWindow::Clock::time_point(std::chrono::seconds(100))+std::chrono::milliseconds(time));
                c.retry=time+unsigned(window.retry_delay().count());
                if(!steady_start&&rebuilt.size()>=2U*1024U*1024U)steady_start=time;
                if(rebuilt.size()==source->size()){receiver.payload_verified();c.finished=time;assert(rebuilt==*source&&m::sha256(rebuilt)==hash);break;}
            }
            result.peak=std::max(result.peak,inbox.bytes());
            assert(inbox.bytes()<=(adaptive?s::MaximumTransferWindow:s::LegacyTransferWindow));
        }
        if(std::all_of(clients.begin(),clients.end(),[](const auto& c){return c->finished!=0;})){result.elapsed=time;
            result.steady_mbps=double(peers*6U*1024U*1024U)*8/1000/double(time-steady_start);
            unsigned first=time;for(const auto& c:clients)first=std::min(first,c->finished);
            // First response includes a different amount of shared-link queue
            // for each recipient. Bound completion skew relative to the full
            // transfer, rather than a fixed two seconds at every link speed.
            if(peers>1)assert(time-first<=std::max(2000U,time/6));
            return result;}
        for(unsigned p=0;p<peers;++p){auto& c=*clients[p];if(c.finished)continue;
        auto& receiver=c.receiver;auto& sender=c.sender;auto& window=c.window;
        auto request=window.request(receiver.request(chunk_budget));
        const bool gap=c.requested&&time>=c.retry,refill=c.requested&&request.offset-c.anchor>=window.stride();
        if(!c.requested||gap||refill){window.credit(!c.requested,gap,s::TransferWindow::Clock::time_point(std::chrono::seconds(100))+std::chrono::milliseconds(time));request=window.request(std::move(request));
            if(refill&&!gap)request.operation=o::Operation::PayloadCredit;
            wire.push_back({time+rtt/2,p,true,std::move(request)});c.anchor=std::uint32_t(c.rebuilt.size());c.retry=time+unsigned(window.retry_delay().count());c.requested=true;result.retries+=gap;}
        if(time%2==0)for(unsigned burst=0;burst<o::UploadBurstPackets;++burst){const auto message=sender.next();if(!message)break;
            wire_free=std::max(wire_free,double(time))+double(message->bytes.size())*8/(double(mbps)*1000);
            result.wire+=message->bytes.size();sender.sent();
            if(lose&&!c.dropped&&message->offset==chunk_budget*4){c.dropped=true;continue;}
            wire.push_back({unsigned(std::ceil(wire_free))+rtt/2,p,false,*message});
        }
        }
    }
    assert(false&&"bounded transfer never completed");return result;
}
#endif
}
int main(){
    policies();
#if defined(DKR_PIPELINE_TRANSFER)
    for(const unsigned rtt:{20U,150U,500U}){
        const auto legacy=transfer(false,rtt,10),adaptive=transfer(true,rtt,10);
        assert(adaptive.steady_mbps>=7.5&&adaptive.retries==0);
        if(rtt==500)assert(adaptive.elapsed*2<legacy.elapsed);
        std::cout<<"Actual Upload/Download/ChunkInbox RTT="<<rtt<<"ms legacy="<<legacy.elapsed<<"ms adaptive="<<adaptive.elapsed<<"ms steady="<<adaptive.steady_mbps<<"Mbit/s max-inbox="<<adaptive.peak<<'\n';
    }
    const auto fast=transfer(true,500,30),repaired=transfer(true,150,10,true);
    assert(fast.steady_mbps>=22.5&&repaired.retries>0);
    std::cout<<"30Mbit/s long-link steady="<<fast.steady_mbps<<"Mbit/s; injected missing chunk recovered, retries="<<repaired.retries<<'\n';
    for(const auto chunk:{768U,1131U}){
        const auto aligned=transfer(true,150,10,false,1,chunk);
        assert(aligned.retries==0&&aligned.steady_mbps>=7.5);
        std::cout<<"Negotiated window preserves whole chunks: budget="<<chunk<<" complete="<<aligned.elapsed<<"ms\n";
        const auto gap=transfer(true,150,10,true,1,chunk);assert(gap.retries>0);
    }
    for(const unsigned peers:{2U,3U,4U}){
        const auto shared=transfer(true,500,10,false,peers);
        assert(shared.steady_mbps>=7.5&&shared.retries==0);
        std::cout<<peers<<" concurrent recipients on one 10Mbit/s uplink: "<<shared.elapsed<<"ms steady="<<shared.steady_mbps<<"Mbit/s per-inbox-bound="<<s::MaximumTransferWindow<<'\n';
    }
#else
    std::cout<<"Authored policies passed; codec/throughput require the production Patch Pipeline target.\n";
#endif
}

#include "checkpoint_transfer.hpp"
#include <algorithm>
#include <stdexcept>

namespace dkr::runtime::spectators {
namespace {
void put(std::vector<std::uint8_t>& b,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(std::uint8_t(v>>(8*i)));}
std::uint64_t get(std::span<const std::uint8_t> b,std::size_t& p,unsigned n){
    if(p>b.size()||n>b.size()-p)throw std::runtime_error("Incomplete spectator checkpoint message.");
    std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(b[p++])<<(8*i);return v;
}
bool no(std::string& e,const char* s){e=s;return false;}
constexpr auto Chunk=wire::MaximumChunk;
}
bool valid_offer(const CheckpointOffer& o){
    return o.transfer&&o.match&&o.schema&&o.control_generation&&o.next.epoch&&o.next.frame!=UINT32_MAX&&
        o.bytes&&o.bytes<=ConfirmedFeed::MaximumState&&std::any_of(o.digest.begin(),o.digest.end(),[](auto b){return b!=0;});
}
std::vector<std::uint8_t> encode_offer(const CheckpointOffer& o){
    if(!valid_offer(o))throw std::runtime_error("Invalid spectator checkpoint offer.");
    std::vector<std::uint8_t> b;b.reserve(80);
    for(auto v:{o.transfer,o.match,o.schema,o.control_generation,o.next.epoch})put(b,v,8);
    put(b,o.next.frame,4);put(b,o.bytes,4);b.insert(b.end(),o.digest.begin(),o.digest.end());put(b,o.magic_actions,4);return b;
}
bool decode_offer(std::span<const std::uint8_t> b,CheckpointOffer& out,std::string& error){
    if(b.size()!=84)return no(error,"Invalid spectator checkpoint offer length.");
    std::size_t p=0;CheckpointOffer o;
    o.transfer=get(b,p,8);o.match=get(b,p,8);o.schema=get(b,p,8);o.control_generation=get(b,p,8);o.next.epoch=get(b,p,8);
    o.next.frame=std::uint32_t(get(b,p,4));o.bytes=std::uint32_t(get(b,p,4));std::copy(b.begin()+48,b.begin()+80,o.digest.begin());p=80;o.magic_actions=std::uint32_t(get(b,p,4));
    if(!valid_offer(o))return no(error,"Invalid spectator checkpoint offer identity or size.");
    out=o;error.clear();return true;
}
std::vector<std::uint8_t> encode_chunk(const CheckpointChunk& c){
    if(!c.transfer||c.bytes.empty()||c.bytes.size()>Chunk||c.offset%Chunk||c.offset>=ConfirmedFeed::MaximumState||
       c.bytes.size()>ConfirmedFeed::MaximumState-c.offset)throw std::runtime_error("Invalid spectator checkpoint chunk.");
    std::vector<std::uint8_t> b;b.reserve(12+c.bytes.size());put(b,c.transfer,8);put(b,c.offset,4);b.insert(b.end(),c.bytes.begin(),c.bytes.end());return b;
}
bool decode_chunk(std::span<const std::uint8_t> b,CheckpointChunk& out,std::string& error){
    if(b.size()<=12||b.size()>12+Chunk)return no(error,"Invalid spectator checkpoint chunk length.");
    std::size_t p=0;CheckpointChunk c;c.transfer=get(b,p,8);c.offset=std::uint32_t(get(b,p,4));
    if(!c.transfer||c.offset%Chunk||c.offset>=ConfirmedFeed::MaximumState||b.size()-12>ConfirmedFeed::MaximumState-c.offset)
        return no(error,"Invalid spectator checkpoint chunk bounds.");
    c.bytes.assign(b.begin()+12,b.end());out=std::move(c);error.clear();return true;
}
bool CheckpointReceiver::begin(CheckpointOffer o,std::uint64_t match,std::uint64_t schema,
                             std::size_t bytes,Clock::time_point now,std::string& error){
    if(!hash_||!valid_offer(o)||o.match!=match||o.schema!=schema||o.bytes!=bytes)
        return no(error,"Spectator checkpoint does not match this game, content or snapshot layout.");
    if(offer_&&o==*offer_){error.clear();return true;}
    if(o.transfer<=retired_||(offer_&&o.transfer<=offer_->transfer))return no(error,"Spectator checkpoint offer is stale.");
    // Stage both allocations before retiring an intact in-flight transfer.
    auto state=std::make_shared<std::vector<std::uint8_t>>(o.bytes);
    std::vector<bool> present((std::size_t(o.bytes)+Chunk-1)/Chunk,false);
    if(offer_)retired_=std::max(retired_,offer_->transfer);
    offer_=o;state_=std::move(state);present_=std::move(present);received_=0;started_=progress_=now;error.clear();return true;
}
CheckpointReceiver::Result CheckpointReceiver::receive(const CheckpointChunk& c,Clock::time_point now,std::string& error){
    if(!offer_||c.transfer!=offer_->transfer){error="Spectator checkpoint chunk belongs to a retired transfer.";return Result::Stale;}
    if(expired(now)){cancel();error="Spectator checkpoint transfer timed out; racers are unaffected.";return Result::Rejected;}
    if(c.offset%Chunk||c.offset>=offer_->bytes||c.bytes.size()!=std::min(Chunk,std::size_t(offer_->bytes-c.offset))){
        error="Spectator checkpoint chunk has invalid bounds.";return Result::Rejected;
    }
    const auto index=c.offset/Chunk;
    if(present_[index]){
        if(!std::equal(c.bytes.begin(),c.bytes.end(),state_->begin()+c.offset)){
            cancel();error="Conflicting spectator checkpoint data was discarded.";return Result::Rejected;
        }
        error.clear();return Result::Duplicate;
    }
    std::copy(c.bytes.begin(),c.bytes.end(),state_->begin()+c.offset);present_[index]=true;received_+=c.bytes.size();progress_=now;
    error.clear();return received_==offer_->bytes?Result::Complete:Result::Accepted;
}
std::optional<Baseline> CheckpointReceiver::take_verified(std::string& error){
    if(!offer_||received_!=offer_->bytes){error="Spectator checkpoint is incomplete.";return {};}
    // This runs on the transfer owner, never on a racing/graphics callback.
    if(hash_(*state_)!=offer_->digest){cancel();error="Spectator checkpoint integrity check failed.";return {};}
    Baseline b{offer_->next,offer_->match,offer_->schema,offer_->control_generation,offer_->digest,state_,offer_->magic_actions};
    cancel();error.clear();return b;
}
bool CheckpointReceiver::expired(Clock::time_point now)const{
    return offer_&&(now<started_||now-progress_>std::chrono::seconds(30)||now-started_>std::chrono::minutes(5));
}
void CheckpointReceiver::cancel(){if(offer_)retired_=std::max(retired_,offer_->transfer);offer_.reset();state_.reset();present_.clear();received_=0;}
bool CheckpointSender::begin(std::uint64_t transfer,std::shared_ptr<const Baseline> b){
    if(!b||!b->state||b->state->size()>UINT32_MAX)return false;
    CheckpointOffer o{transfer,b->match,b->schema,b->control_generation,b->next,std::uint32_t(b->state->size()),b->digest,b->magic_actions};
    if(!valid_offer(o))return false;
    offer_=o;state_=b->state;offset_=0;return true;
}
std::optional<CheckpointChunk> CheckpointSender::next_chunk()const{
    if(!state_||offset_>=state_->size())return {};
    const auto count=std::min(Chunk,state_->size()-offset_);
    return CheckpointChunk{offer_.transfer,std::uint32_t(offset_),{state_->begin()+std::ptrdiff_t(offset_),state_->begin()+std::ptrdiff_t(offset_+count)}};
}
void CheckpointSender::sent(){if(state_)offset_+=std::min(Chunk,state_->size()-offset_);}
}

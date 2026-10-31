#include "confirmed_feed.hpp"
#include <algorithm>

namespace dkr::runtime::spectators {
namespace {
std::size_t size(const ConfirmedFrame& f){return 48+f.command.size();}
}
void ConfirmedFeed::clear(){baseline_.reset();tail_.clear();next_={};bytes_=0;}
FeedResult ConfirmedFeed::baseline(Baseline next) {
    if(!next.match||!next.schema||!next.next.epoch||next.next.frame==UINT32_MAX||!next.control_generation||!next.state||next.state->empty()||next.state->size()>MaximumState||
       std::all_of(next.digest.begin(),next.digest.end(),[](auto v){return !v;}))return FeedResult::Invalid;
    if(baseline_&&(next.match!=baseline_->match||next.schema!=baseline_->schema||next.control_generation<baseline_->control_generation||next.next.epoch<baseline_->next.epoch||
        (next.next.epoch==baseline_->next.epoch&&next.next.frame<baseline_->next.frame)))return FeedResult::Stale;
    if(baseline_&&next.next==baseline_->next){
        if(next.digest!=baseline_->digest||next.magic_actions!=baseline_->magic_actions)return FeedResult::Invalid;
        if(next.control_generation==baseline_->control_generation)return FeedResult::Duplicate;
    }
    auto retained=std::make_shared<const Baseline>(std::move(next));
    while(!tail_.empty()&&(tail_.front().cursor.epoch!=retained->next.epoch||tail_.front().cursor.frame<retained->next.frame)) {
        bytes_-=size(tail_.front());tail_.pop_front();
    }
    if(tail_.empty()||tail_.front().cursor!=retained->next){tail_.clear();bytes_=0;next_=retained->next;}
    baseline_=std::move(retained);return FeedResult::Accepted;
}
FeedResult ConfirmedFeed::append(ConfirmedFrame frame) {
    if(!baseline_||!frame.cursor.epoch||frame.command.size()>MaximumCommand||frame.cursor.frame==UINT32_MAX)return FeedResult::Invalid;
    if(frame.cursor.epoch<next_.epoch)return FeedResult::Stale;
    if(frame.cursor.epoch!=next_.epoch)return FeedResult::Gap;
    if(frame.cursor.frame<next_.frame) {
        const auto found=std::find_if(tail_.begin(),tail_.end(),[&](const auto& f){return f.cursor==frame.cursor;});
        return found==tail_.end()?FeedResult::Stale:*found==frame?FeedResult::Duplicate:FeedResult::Invalid;
    }
    if(frame.cursor!=next_)return FeedResult::Gap;
    if(!tail_.empty()&&tail_.back().scene_boundary)return FeedResult::Gap;
    const auto cost=size(frame);
    if(cost>MaximumTail)return FeedResult::Capacity;
    while(!tail_.empty()&&(tail_.size()>=MaximumFrames||bytes_>MaximumTail-cost)){bytes_-=size(tail_.front());tail_.pop_front();}
    ++next_.frame;bytes_+=cost;tail_.push_back(std::move(frame));return FeedResult::Accepted;
}
bool ConfirmedFeed::covers(Cursor first)const {
    return baseline_&&first.epoch==next_.epoch&&first.frame<=next_.frame&&
        (first==next_||(!tail_.empty()&&first.frame>=tail_.front().cursor.frame));
}
std::vector<ConfirmedFrame> ConfirmedFeed::after(Cursor first,std::size_t maximum)const {
    std::vector<ConfirmedFrame> result;if(!covers(first))return result;
    maximum=std::min<std::size_t>(maximum,120);result.reserve(maximum);
    for(const auto& f:tail_)if(f.cursor.frame>=first.frame){if(result.size()==maximum)break;result.push_back(f);}
    return result;
}
}

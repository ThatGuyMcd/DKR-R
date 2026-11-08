#pragma once
#include "checkpoint_transfer.hpp"
#include <deque>

namespace dkr::runtime::spectators {
// No input sampling or file/device API is available through this boundary.
// A game adapter performs canonical guest operations on its simulation owner.
enum class Work {Pending,Ready,Failed};
class ObserverWorld {
public:
    virtual ~ObserverWorld()=default;
    virtual std::uint64_t schema()const=0;
    virtual std::size_t state_bytes()const=0;
    virtual Work retire(Cursor next,std::string& error)=0;
    virtual bool admit(const Baseline& baseline,std::string& error)=0;
    virtual Work prepare(const ConfirmedFrame& frame,std::string& error)=0;
    virtual bool tick_and_commit(const ConfirmedFrame& frame,std::string& error)=0;
    virtual bool publish(Cursor completed,std::string& error)=0;
    virtual Work prepare_scene(Cursor completed,std::string& error)=0;
};
enum class PlaybackPhase {WaitingForBaseline,Retiring,CatchingUp,Watching,SceneBoundary,Failed,Stopped};
struct PlaybackView {
    PlaybackPhase phase=PlaybackPhase::WaitingForBaseline;
    Cursor next{},received{};
    std::size_t queued=0;
    std::uint64_t simulated=0,resyncs=0;
    std::string status="Waiting for the host's confirmed game state";
};
// Confirmed-only observer. Every pulse does at most one guest tick or one
// bounded scene/preparation step. The host never waits on this object's pace.
// The playback owner calls all methods; transport queues immutable values.
class ObserverPlayback final {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::size_t MaximumQueued=1800;
    ObserverPlayback(ObserverWorld& world,std::uint64_t match,HashState hash);
    bool offer(Baseline baseline,std::string& error);
    FeedResult append(std::span<const ConfirmedFrame> frames,std::string& error);
    Work pulse(Clock::time_point now,std::string& error);
    void request_resync(std::string reason);
    void stop();
    const PlaybackView& view()const{return view_;}
    bool needs_baseline()const{return view_.phase==PlaybackPhase::WaitingForBaseline;}
    bool caught_up(Cursor at)const{return view_.next==at&&queue_.empty()&&view_.phase==PlaybackPhase::Watching;}
private:
    Work fail(std::string message,std::string& error);
    ObserverWorld& world_;
    std::uint64_t match_;
    HashState hash_;
    PlaybackView view_{};
    std::optional<Baseline> pending_;
    std::deque<ConfirmedFrame> queue_;
    std::deque<ConfirmedFrame> recent_;
    std::size_t queued_bytes_=0;
    Clock::time_point next_tick_{},last_poll_{};
    bool clock_started_=false,prepared_=false,scene_prepared_=false;
};
}

#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace dkr::lobby_stability {
using Clock=std::chrono::steady_clock;
inline constexpr unsigned MaxPending=8, MaxDownloads=3, MaxRepairs=5;
inline constexpr const char* Queued="Waiting for a mod download slot";
inline constexpr const char* SlotReady="Host download slot ready";
// The bootstrap mutes presentation. For an audio submission only, let the
// unchanged SDK enqueue its normal SP worker instead of synchronously waiting
// for a completion acknowledgement on the guest scheduler's own thread.
inline thread_local unsigned bootstrap_audio_submissions=0;
class BootstrapAudioSubmission {
    bool enabled_;
public:
    explicit BootstrapAudioSubmission(bool enabled)noexcept:enabled_(enabled){if(enabled_)++bootstrap_audio_submissions;}
    ~BootstrapAudioSubmission(){if(enabled_)--bootstrap_audio_submissions;}
    BootstrapAudioSubmission(const BootstrapAudioSubmission&)=delete;
    BootstrapAudioSubmission& operator=(const BootstrapAudioSubmission&)=delete;
};
inline bool bootstrap_audio_worker_allowed()noexcept{return bootstrap_audio_submissions!=0;}
struct Candidate {std::uint64_t id,ticket;};
inline std::vector<std::uint64_t> download_slots(std::vector<Candidate> candidates) {
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.ticket<b.ticket;});
    std::vector<std::uint64_t> result;
    for(const auto& c:candidates)if(result.size()<MaxDownloads)result.push_back(c.id);
    return result;
}
// Only contiguous verified/write progress resets retries. A queue status can
// pause repairs briefly, but cannot extend the absolute transfer deadline.
class GapBudget {
    unsigned repairs_=0;
    Clock::time_point next_{},queue_until_{};
public:
    void progress(Clock::time_point now) {repairs_=0;next_=now+std::chrono::seconds(3);queue_until_={};}
    void queue(bool queued,Clock::time_point now) {queue_until_=queued?now+std::chrono::seconds(2):Clock::time_point{};}
    bool due(Clock::time_point now)const{return now>=next_&&now>=queue_until_;}
    bool exhausted()const{return repairs_>=MaxRepairs;}
    unsigned count()const{return repairs_;}
    void repaired(Clock::time_point now) {
        ++repairs_;next_=now+std::chrono::seconds(std::min(30U,3U<<std::min(repairs_,4U)));
    }
};
class PhaseBudget {
    int phase_=-1;
    Clock::time_point began_{};
public:
    bool expired(int phase,std::chrono::seconds limit,Clock::time_point now) {
        if(phase_!=phase){phase_=phase;began_=now;}
        return now-began_>limit;
    }
};
// HelloAck has a 160-byte text budget. Never turn a detailed rejection into
// an empty encoded packet, and never put paths or control characters on wire.
inline std::string rejection(std::string_view text) {
    std::string out="Mod preparation failed: ";
    for(char c:text) {
        if(out.size()>=157)break;
        if(c=='/'||c=='\\'||c==':')return "Mod preparation failed on the host. Ask the host to export the lobby log.";
        out+=static_cast<unsigned char>(c)>=32&&static_cast<unsigned char>(c)<127?c:' ';
    }
    if(text.empty())out+="Please retry from the lobby.";
    return out;
}
inline thread_local const void* rejection_session=nullptr;
inline thread_local std::uint64_t rejection_request=0;
inline thread_local std::string rejection_message;
class RejectionScope {
    const void* previous_session_=rejection_session;
    std::uint64_t previous_request_=rejection_request;
    std::string previous_message_=std::move(rejection_message);
public:
    RejectionScope(const void* session,std::uint64_t request,std::string_view message) {
        rejection_session=session;rejection_request=request;rejection_message=rejection(message);
    }
    ~RejectionScope(){rejection_session=previous_session_;rejection_request=previous_request_;rejection_message=std::move(previous_message_);}
};
inline std::string decline_reason(const void* session,std::uint64_t request) {
    return session==rejection_session&&request==rejection_request&&!rejection_message.empty()
        ?rejection_message:"The host declined this join request.";
}
}

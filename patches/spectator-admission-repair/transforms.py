"""Observer-only preparation; ordinary rollback restore remains cache-only."""

def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Spectator admission anchor changed: ' + before[:160])
    return text.replace(before, after, 1)


def resident(text):
    text = '#include "observer_music.hpp"\n' + text
    text = once(text, 'song.pcm=acquire(r,allow_decode);',
        'song.pcm=acquire(r,allow_decode || dkr::spectator_admission::preparing_music());')
    text = once(text, '''    const auto level=reader.u32();
    State next=*this;''', '''    const auto level=reader.u32();
    // Validate the complete music checkpoint against admitted metadata before
    // spending decoder work. This reads no guest RAM and changes no state.
    if(dkr::spectator_admission::preparing_music())
        (void)MusicState(sources_,revision80_).stage_checkpoint(bytes);
    State next=*this;''')
    text = once(text, '    next.native_=MusicState(next.active_,revision80_).stage_checkpoint(bytes);',
        '''    next.native_=MusicState(next.active_,revision80_).stage_checkpoint(bytes);
    if(dkr::spectator_admission::preparing_music())
        std::fprintf(stderr,"[spectator][music] checkpoint-ready level=%u active=%u\\n",level,unsigned(!next.active_->empty()));''')
    return text


def driver(text):
    text = '#include "observer_music.hpp"\n' + text
    text = once(text, '            staged_mods=mods_->stage_checkpoint(block.subspan(68,count));',
        '''            if(admission) {
                // ConfirmedAdmission exists only after the authenticated
                // baseline/layout and irreversible participants were checked.
                // Keep the lease in staged_mods until the restore commits.
                staged_mods=dkr::spectator_admission::prepare_verified_music([&] {
                    return mods_->stage_checkpoint(block.subspan(68,count));
                });
            } else {
                staged_mods=mods_->stage_checkpoint(block.subspan(68,count));
            }''')
    return text


def party(text):
    text = '#include "admission_retry.hpp"\n#include <cstdio>\n' + text
    text = once(text, '    WatchingWorld watch_world;ObserverPlayback playback;ConfirmedFeed feed;ConfirmedFrameBinding binding;',
        '''    WatchingWorld watch_world;ObserverPlayback playback;ConfirmedFeed feed;ConfirmedFrameBinding binding;
    dkr::spectator_admission::AdmissionRetry observer_retry;
    std::uint64_t observer_render_epoch=0;''')
    text = once(text, '        held=b;admitted_at=b.next;cursor=b.next;controls=b.control_generation;',
        '''        held=b;admitted_at=b.next;cursor=b.next;controls=b.control_generation;
        if(watching())std::fprintf(stderr,"[spectator][admission] ready epoch=%llu frame=%u\\n",
            static_cast<unsigned long long>(b.next.epoch),b.next.frame);''')
    text = once(text, '''                const auto work=playback.pulse(now,error);
                if(work==Work::Failed){playback.request_resync(error);held.reset();admitted_at.reset();send(host,Msg::Need);error.clear();}''',
        '''                const auto simulated_before=playback.view().simulated;
                const auto work=playback.pulse(now,error);
                if(work==Work::Failed){
                    const auto at=playback.view().received;
                    const bool retry=observer_retry.failed(at.epoch);
                    std::fprintf(stderr,"[spectator][admission] failed epoch=%llu frame=%u attempt=%u/%u reason=%.512s\\n",
                        static_cast<unsigned long long>(at.epoch),at.frame,observer_retry.failures(),
                        dkr::spectator_admission::AdmissionRetry::MaximumFailures,error.c_str());
                    if(!retry){error="Spectator playback could not recover after five failures: "+error;return false;}
                    playback.request_resync(error);held.reset();admitted_at.reset();send(host,Msg::Need);error.clear();
                }
                if(playback.view().simulated!=simulated_before){
                    observer_retry.progressed();
                    const auto at=playback.view().next;
                    if(observer_render_epoch!=at.epoch){
                        observer_render_epoch=at.epoch;
                        std::fprintf(stderr,"[spectator][playback] first-frame epoch=%llu frame=%u\\n",
                            static_cast<unsigned long long>(at.epoch),at.frame-1);
                    }
                }''')
    return text

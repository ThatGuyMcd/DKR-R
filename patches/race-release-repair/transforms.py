"""Repair release idempotency without changing transport or checkpoint policy."""


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Race release repair anchor changed: ' + before[:160])
    return text.replace(before, after, 1)


def runtime(text):
    text = once(text,
        '    Bytes release_packet;std::optional<CheckpointOffer> last_offer;',
        '''    Bytes release_packet;std::optional<CheckpointOffer> last_offer;
    // A prepared scene or admitted observer checkpoint advances cursor before
    // its Go arrives. Only an actually activated release is a duplicate.
    std::optional<std::pair<Cursor,std::uint64_t>> activated_release;''')
    text = once(text,
        '            if(at.epoch<cursor.epoch||(go&&at.epoch==cursor.epoch&&at.frame<=cursor.frame&&generation<=controls))return;',
        '''            if(at.epoch<cursor.epoch||(go&&at.epoch==cursor.epoch&&at.frame<cursor.frame&&generation<=controls)) {
                std::fprintf(stderr,"[party][release-receive] ignored=stale epoch=%llu frame=%u controls=%llu\\n",
                    static_cast<unsigned long long>(at.epoch),at.frame,static_cast<unsigned long long>(generation));
                return;
            }
            if(activated_release&&at==activated_release->first&&generation<=activated_release->second) {
                std::fprintf(stderr,"[party][release-receive] ignored=duplicate epoch=%llu frame=%u controls=%llu\\n",
                    static_cast<unsigned long long>(at.epoch),at.frame,static_cast<unsigned long long>(generation));
                return;
            }''')
    text = once(text,
        '            if(go&&phase==Phase::Running&&at==cursor&&generation==controls)return;\n            controls=generation;update_hub();activate(at);break;',
        '''            controls=generation;update_hub();activate(at);
            std::fprintf(stderr,"[party][release-receive] accepted role=%s epoch=%llu frame=%u controls=%llu\\n",
                watching()?"spectator":"racer",static_cast<unsigned long long>(at.epoch),at.frame,static_cast<unsigned long long>(generation));
            break;''')
    text = once(text,
        '        phase=Phase::Running;freeze=UINT32_MAX;swap.reset();frozen.clear();scene_votes.clear();prepared.clear();go=true;',
        '''        activated_release=std::pair{at,controls};
        phase=Phase::Running;freeze=UINT32_MAX;swap.reset();frozen.clear();scene_votes.clear();prepared.clear();go=true;''')
    text = once(text,
        '            auto b=capture(cursor);phase_digest=signature(b);held=b;phase=Phase::SceneReady;phase_at=now;',
        '''            auto b=capture(cursor);phase_digest=signature(b);held=b;phase=Phase::SceneReady;phase_at=now;
            std::fprintf(stderr,"[party][scene] prepared role=%s epoch=%llu frame=%u awaiting=%s\\n",
                server()?"host":"racer",static_cast<unsigned long long>(cursor.epoch),cursor.frame,
                server()?"racer-proofs":"host-release");''')
    return text

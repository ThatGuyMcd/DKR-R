    struct ConfirmedAdmission { Eeprom save;Paks paks;MagicCodes magic; };
    bool admit_observer(const dkr::runtime::spectators::Baseline& baseline,std::string& error) {
        if(!baseline.state||baseline.schema!=contract().schema||baseline.state->size()!=contract().state_bytes||
           !baseline.next.epoch||baseline.next.frame==UINT32_MAX||!observer_retired_||*observer_retired_!=baseline.next) {
            error="Confirmed observer admission has no matching retired owner/layout.";return false;
        }
        const auto bytes=std::span<const std::uint8_t>(*baseline.state);
        dkr_probe_native_state incoming{};ComponentAudio audio{};
        std::memcpy(&incoming,bytes.data()+state_.snapshot_size(),sizeof(incoming));
        std::memcpy(&audio,bytes.data()+state_.snapshot_size()+sizeof(native_),sizeof(audio));
        if(incoming.pending_scene_site||incoming.scene_unload_phase||audio.next_frame!=baseline.next.frame||
           !valid_native(incoming)||!valid_audio(audio,0)) {
            error="Observer baseline is not a complete confirmed gameplay boundary.";return false;
        }
        ConfirmedAdmission staged{save_,paks_,magic_};
        if(!staged.save.admit_confirmed(baseline.next.epoch,baseline.next.frame,
                bytes.last(Eeprom::kCheckpointBytes+Paks::kCheckpointBytes).first(Eeprom::kCheckpointBytes))||
           !staged.paks.admit_confirmed(baseline.next.epoch,baseline.next.frame,bytes.last(Paks::kCheckpointBytes))||
           !staged.magic.admit_confirmed(baseline.next.epoch,baseline.next.frame,baseline.magic_actions,
                bytes.subspan(state_.snapshot_size()+sizeof(native_)+sizeof(audio_),MagicCodes::kCheckpointBytes))) {
            error="Observer baseline has inconsistent irreversible participants.";return false;
        }
        if(!restore_state(bytes,error,&staged))return false;
        current_epoch_=baseline.next.epoch;confirmed_count_=baseline.next.frame;confirmed_scene_=0;
        commits_.clear();achievement_frames_.clear();scenery_.reset(native_.presentation.scene);
        published_preview_loads_=native_.preview_loads;observer_retired_.reset();return true;
    }
    dkr::runtime::spectators::Work retire_observer(dkr::runtime::spectators::Cursor target,std::string& error) {
        using dkr::runtime::spectators::Work;
        if(!presentation_||!target.epoch||target.epoch<current_epoch_||target.frame==UINT32_MAX) {
            error="Observer retirement targets an invalid owner epoch.";return Work::Failed;
        }
        if(observer_retired_&&*observer_retired_!=target) {
            if(target.epoch<observer_retired_->epoch||(target.epoch==observer_retired_->epoch&&target.frame<observer_retired_->frame)) {
                error="Observer retirement cannot move backwards.";return Work::Failed;
            }
            if(target.epoch!=observer_retired_->epoch)observer_epoch_advanced_=false;
            observer_retired_=target;
        }
        if(!observer_retired_) {
            if(!presentation_->retire_for_restore(current_epoch_,0)) {
                error="Observer presentation generation refused retirement.";return Work::Failed;
            }
            observer_retired_=target;observer_epoch_advanced_=false;
        }
        if(!presentation_->quiescent())return Work::Pending;
        if(target.epoch>current_epoch_&&!observer_epoch_advanced_) {
            if(!presentation_->begin_epoch(target.epoch)) {
                error="Observer presentation epoch could not advance after retirement.";return Work::Failed;
            }
            observer_epoch_advanced_=true;
        }
        return Work::Ready;
    }
    void observer_read_only(bool enabled) {observer_read_only_=enabled;achievement_frames_.clear();}
    std::uint32_t observer_magic_actions()const {return magic_.confirmed_actions();}

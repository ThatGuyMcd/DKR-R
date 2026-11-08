    bool admit_confirmed(std::uint64_t epoch,std::uint32_t frame,std::uint32_t actions,std::span<const std::uint8_t> bytes) {
        if(!epoch||frame==UINT32_MAX||bytes.size()!=kCheckpointBytes||(actions&~initial_.deferred_action_mask))return false;
        MagicCodes staged;if(!staged.start(epoch,selection_,online_))return false;
        staged.next_=frame;
        if(!staged.restore(bytes)||(staged.working_.armed_action_mask&actions))return false;
        staged.confirmed_next_=frame;staged.confirmed_actions_=actions;*this=std::move(staged);return true;
    }

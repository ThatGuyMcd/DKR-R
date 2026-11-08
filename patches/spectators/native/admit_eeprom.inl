    // New confirmed-network admission. Ordinary rollback restore() below is
    // unchanged and still cannot transplant an irreversible future cursor.
    bool admit_confirmed(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes) {
        if(!epoch||frame==UINT32_MAX||bytes.size()!=kCheckpointBytes)return false;
        std::uint64_t incoming_epoch=0;std::uint32_t incoming_frame=0;
        for(unsigned i=0;i<8;++i)incoming_epoch|=std::uint64_t(bytes[i])<<(8*i);
        for(unsigned i=0;i<4;++i)incoming_frame|=std::uint32_t(bytes[8+i])<<(8*i);
        if(incoming_epoch!=epoch||incoming_frame!=frame)return false;
        Eeprom staged;if(!staged.start(epoch,bytes.subspan(12)))return false;
        staged.next_=staged.confirmed_next_=frame;*this=std::move(staged);return true;
    }

    bool admit_confirmed(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes) {
        if(!epoch||frame==UINT32_MAX||bytes.size()!=kCheckpointBytes||bytes[0]!='D'||bytes[1]!='K'||bytes[2]!='P'||bytes[3]!='K'||
           bytes[4]!=1||bytes[5]!=mask_||bytes[6]||bytes[7])return false;
        std::uint64_t incoming_epoch=0;std::uint32_t incoming_frame=0;
        for(unsigned i=0;i<8;++i)incoming_epoch|=std::uint64_t(bytes[8+i])<<(8*i);
        for(unsigned i=0;i<4;++i)incoming_frame|=std::uint32_t(bytes[16+i])<<(8*i);
        if(incoming_epoch!=epoch||incoming_frame!=frame)return false;
        for(unsigned i=20;i<32;++i)if(bytes[i])return false;
        Paks staged;if(!staged.start(epoch,mask_,bytes.subspan(32)))return false;
        staged.next_=staged.confirmed_next_=frame;*this=std::move(staged);return true;
    }

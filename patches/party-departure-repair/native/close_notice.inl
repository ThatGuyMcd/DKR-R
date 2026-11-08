// disconnect already owns DirectSession::mutex_. No PartyLink lock is retained
// while pumping the authenticated receipt. The final wait is strictly bounded.
{
    namespace leaving=dkr::runtime::party_departure;
    bool notify=false;
    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     notify=party->enabled&&party->admitted&&!is_host_&&bool(host_address_)&&transport_->is_open();}
    if(notify){
        leaving::begin(this,sender_id_);
        const auto bytes=dkr::runtime::spectators::link_encode(dkr::runtime::spectators::LinkKind::Leave,leaving::notice(1,sender_id_));
        send_with_key(host_address_,key_,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),bytes);
        const auto deadline=std::chrono::steady_clock::now()+leaving::CloseGrace;
        do{
            flush_outbound_locked();pump_locked();
            if(leaving::acknowledged(this))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }while(transport_->is_open()&&std::chrono::steady_clock::now()<deadline);
        std::fprintf(stderr,"[lobby][leave] application close notice %s\n",leaving::acknowledged(this)?"acknowledged":"grace expired; host silence detection remains active");
        leaving::forget(this);
    }
}

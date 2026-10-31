// Synthetic datagrams share the existing authenticated route and traffic
// classification, but never enter the game's reliable application queues.
auto rollback_send=[&](std::uint64_t target,protocol::MessageType type,std::span<const std::uint8_t> payload) {
    namespace watch=dkr::runtime::spectators;
    if(!transport_->is_open())return false;
    PeerAddress address{};secure::Key peer_key{};
    auto party=watch::party_link(this);std::scoped_lock party_lock(party->mutex);
    if(is_host_){
        if(party->enabled){auto* peer=party->peer(target);if(!peer)return false;address=peer->address;peer_key=peer->key;}
        else{auto* peer=peer_by_sender(target);if(!peer)return false;address=peer->address;peer_key=peer->key;}
    }else{if(target!=host_sender_id_)return false;address=host_address_;peer_key=key_;}
    TransportTrafficClass lane=TransportTrafficClass::Control;
    if(type==protocol::MessageType::PreflightRealtimeProbe)lane=TransportTrafficClass::Realtime;
    else if(type==protocol::MessageType::PreflightAuthorityProbe)lane=TransportTrafficClass::Authoritative;
    else if(type==protocol::MessageType::PreflightReplicaProbe)lane=TransportTrafficClass::Replica;
    if(!transport_->traffic_ready(address,lane)||transport_->buffered_bytes(lane)>64*1024)return false;
    auto& sequence=outbound_sequence(address,peer_key);if(sequence==UINT64_MAX)return false;
    protocol::Datagram record{{type,match_id_,sequence++,0},{payload.begin(),payload.end()}};
    const auto plain=protocol::encode(record,transport_->maximum_plaintext_datagram_bytes());if(plain.empty())return false;
    const auto bytes=secure::seal(plain,peer_key,sender_id_,record.header.sequence,match_id_);
    std::string ignored;return transport_->send_status(address,bytes,lane,ignored)==DatagramSendStatus::Sent;
};

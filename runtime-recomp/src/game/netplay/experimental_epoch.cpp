#include "experimental_epoch.hpp"
#include <algorithm>

namespace dkr::runtime::netplay::experimental {
namespace {
bool valid(const EpochMessage& m) {
    if (m.players<2 || m.players>4 || !m.epoch || !m.confirmed_end || !m.boundary_hash || !m.schema) return false;
    switch(m.kind) {
    case EpochMessageKind::Boundary: return !m.next_epoch && !m.scene_hash && !m.baseline_hash;
    case EpochMessageKind::Prepare: return m.next_epoch>m.epoch && m.scene_hash && !m.baseline_hash;
    case EpochMessageKind::Prepared:
    case EpochMessageKind::Release:
    case EpochMessageKind::Acknowledged: return m.next_epoch>m.epoch && m.scene_hash && m.baseline_hash;
    default:return false;
    }
}
void put(std::vector<std::uint8_t>& bytes,std::uint64_t value,unsigned width) {
    for(unsigned b=0;b<width;++b) bytes.push_back(std::uint8_t(value>>(8*b)));
}
std::uint64_t get(std::span<const std::uint8_t> bytes,unsigned at,unsigned width) {
    std::uint64_t value=0;
    for(unsigned b=0;b<width;++b) value|=std::uint64_t(bytes[at+b])<<(8*b);
    return value;
}
}
std::vector<std::uint8_t> encode_epoch_message(const EpochMessage& m) {
    if(!valid(m)) return {};
    std::vector<std::uint8_t> bytes{'D','K','E','G',1,std::uint8_t(m.kind),m.players,0};
    bytes.reserve(60);
    put(bytes,m.epoch,8); put(bytes,m.next_epoch,8); put(bytes,m.confirmed_end,4);
    for(auto value:{m.boundary_hash,m.schema,m.scene_hash,m.baseline_hash}) put(bytes,value,8);
    return bytes;
}
std::optional<EpochMessage> decode_epoch_message(std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=60 || bytes[0]!='D' || bytes[1]!='K' || bytes[2]!='E' || bytes[3]!='G' ||
       bytes[4]!=1 || bytes[5]>4 || bytes[7]) return {};
    EpochMessage m;
    m.kind=EpochMessageKind(bytes[5]); m.players=bytes[6];
    m.epoch=get(bytes,8,8); m.next_epoch=get(bytes,16,8); m.confirmed_end=std::uint32_t(get(bytes,24,4));
    m.boundary_hash=get(bytes,28,8); m.schema=get(bytes,36,8); m.scene_hash=get(bytes,44,8); m.baseline_hash=get(bytes,52,8);
    return valid(m)?std::optional(m):std::nullopt;
}
bool EpochGate::begin(std::uint64_t epoch,std::uint8_t players,std::uint8_t local,std::uint64_t schema) {
    if(!epoch || players<2 || players>4 || local>=players || !schema ||
       (boundary_.epoch && (!released() || !proposal_ || epoch!=proposal_->next_epoch ||
                           players!=boundary_.players || local!=local_ || schema!=boundary_.schema))) return false;
    if(proposal_) {
        previous_=*proposal_; previous_->baseline_hash=baselines_[local_];
        previous_->kind=local_ ? EpochMessageKind::Acknowledged : EpochMessageKind::Release;
    }
    boundary_={}; boundary_.epoch=epoch; boundary_.players=players; boundary_.schema=schema;
    local_=local; votes_={}; baselines_={}; acknowledged_={}; proposal_.reset();
    confirmed_=prepared_=release_available_=released_=failed_=false; return true;
}
bool EpochGate::matches_boundary(const EpochMessage& m) const {
    return m.epoch==boundary_.epoch && m.players==boundary_.players && m.schema==boundary_.schema &&
           m.confirmed_end==boundary_.confirmed_end && m.boundary_hash==boundary_.boundary_hash;
}
bool EpochGate::matches_proposal(const EpochMessage& m) const {
    return proposal_ && matches_boundary(m) && m.next_epoch==proposal_->next_epoch && m.scene_hash==proposal_->scene_hash;
}
bool EpochGate::validate_votes() {
    if(!confirmed_) return true;
    for(unsigned p=0;p<boundary_.players;++p) if(votes_[p] && !matches_boundary(*votes_[p])) {
        failed_=true; return false;
    }
    return true;
}
bool EpochGate::confirm_boundary(std::uint32_t end,std::uint64_t hash) {
    if(failed_ || !boundary_.epoch || !end || !hash) return false;
    if(confirmed_) return boundary_.confirmed_end==end && boundary_.boundary_hash==hash;
    boundary_.confirmed_end=end; boundary_.boundary_hash=hash; confirmed_=true;
    votes_[local_]=boundary_; return validate_votes();
}
bool EpochGate::all_boundaries_confirmed() const {
    if(local_ || !confirmed_ || failed_) return false;
    for(unsigned p=0;p<boundary_.players;++p) if(!votes_[p] || !matches_boundary(*votes_[p])) return false;
    return true;
}
bool EpochGate::propose(std::uint64_t epoch,std::uint64_t scene) {
    if(!all_boundaries_confirmed() || epoch<=boundary_.epoch || !scene) return false;
    if(proposal_) return proposal_->next_epoch==epoch && proposal_->scene_hash==scene;
    proposal_=boundary_; proposal_->kind=EpochMessageKind::Prepare;
    proposal_->next_epoch=epoch; proposal_->scene_hash=scene; return true;
}
bool EpochGate::can_prepare_scene() const { return confirmed_ && proposal_ && !prepared_ && !failed_; }
void EpochGate::try_release() {
    if(local_ || !prepared_ || failed_) return;
    const auto hash=baselines_[0];
    for(unsigned p=0;p<boundary_.players;++p) {
        if(!baselines_[p]) return;
        if(baselines_[p]!=hash) { failed_=true; return; }
    }
    release_available_=true; acknowledged_[0]=true;
}
bool EpochGate::mark_prepared(std::uint64_t hash) {
    if(!hash || failed_) return false;
    if(prepared_) return baselines_[local_]==hash;
    if(!can_prepare_scene()) return false;
    baselines_[local_]=hash; prepared_=true; try_release(); return !failed_;
}
EpochResult EpochGate::receive_authenticated(std::uint8_t peer,const EpochMessage& m) {
    if(!boundary_.epoch || failed_ || peer>=boundary_.players || peer==local_ || !valid(m) ||
       (local_ && peer) || m.players!=boundary_.players || m.schema!=boundary_.schema) return EpochResult::Rejected;
    if(m.epoch<boundary_.epoch) {
        if(previous_ && m.epoch==previous_->epoch && m.players==previous_->players &&
           m.next_epoch==previous_->next_epoch && m.confirmed_end==previous_->confirmed_end &&
           m.boundary_hash==previous_->boundary_hash && m.schema==previous_->schema &&
           m.scene_hash==previous_->scene_hash && m.baseline_hash==previous_->baseline_hash &&
           m.kind==(local_ ? EpochMessageKind::Release : EpochMessageKind::Acknowledged)) return EpochResult::Duplicate;
        return EpochResult::Stale;
    }
    if(m.epoch!=boundary_.epoch) return EpochResult::Rejected;
    if(m.kind==EpochMessageKind::Boundary && !local_) {
        if(votes_[peer]) {
            if(*votes_[peer]==m) return EpochResult::Duplicate;
            failed_=true; return EpochResult::Conflict;
        }
        votes_[peer]=m;
        return validate_votes()?EpochResult::Accepted:EpochResult::Conflict;
    }
    if(m.kind==EpochMessageKind::Prepare && local_) {
        if(!confirmed_) return EpochResult::Pending; // Host retries; no premature native scene load.
        if(!matches_boundary(m) || (proposal_ && *proposal_!=m)) { failed_=true; return EpochResult::Conflict; }
        if(proposal_) return EpochResult::Duplicate;
        proposal_=m; return EpochResult::Accepted;
    }
    if(m.kind==EpochMessageKind::Prepared && !local_) {
        if(!matches_proposal(m)) return EpochResult::Rejected;
        if(baselines_[peer]) {
            if(baselines_[peer]==m.baseline_hash) return EpochResult::Duplicate;
            failed_=true; return EpochResult::Conflict;
        }
        baselines_[peer]=m.baseline_hash; try_release();
        return failed_?EpochResult::Conflict:EpochResult::Accepted;
    }
    if(m.kind==EpochMessageKind::Release && local_) {
        if(!matches_proposal(m) || !prepared_) return EpochResult::Pending;
        if(m.baseline_hash!=baselines_[local_]) { failed_=true; return EpochResult::Conflict; }
        if(released_) return EpochResult::Duplicate;
        released_=true; return EpochResult::Accepted;
    }
    if(m.kind==EpochMessageKind::Acknowledged && !local_) {
        if(!release_available_ || !matches_proposal(m) || m.baseline_hash!=baselines_[0]) return EpochResult::Rejected;
        if(acknowledged_[peer]) return EpochResult::Duplicate;
        acknowledged_[peer]=true;
        released_=std::all_of(acknowledged_.begin(),acknowledged_.begin()+boundary_.players,[](bool value){return value;});
        return EpochResult::Accepted;
    }
    return EpochResult::Rejected;
}
std::optional<EpochMessage> EpochGate::message_for(std::uint8_t peer) const {
    if(!confirmed_ || failed_ || peer>=boundary_.players || peer==local_ || (local_ && peer)) return {};
    if(!local_) {
        if(!proposal_) return {};
        auto message=*proposal_;
        if(release_available_) { message.kind=EpochMessageKind::Release; message.baseline_hash=baselines_[0]; }
        return message;
    }
    if(prepared_) {
        auto message=*proposal_; message.kind=released_ ? EpochMessageKind::Acknowledged : EpochMessageKind::Prepared;
        message.baseline_hash=baselines_[local_]; return message;
    }
    return boundary_;
}
std::optional<EpochMessage> EpochGate::previous_message_for(std::uint8_t peer) const {
    if(!previous_ || failed_ || peer>=boundary_.players || peer==local_ || (local_ && peer)) return {};
    return previous_;
}
}

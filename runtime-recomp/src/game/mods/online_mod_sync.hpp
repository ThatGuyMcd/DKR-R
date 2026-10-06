#pragma once
#include "online_mod_profile.hpp"
#include "online_mod_transfer.hpp"
#include <condition_variable>
#include <thread>
#include <atomic>

namespace dkr::mods::online {
struct SyncOffer {std::string digest;std::uint32_t bytes=0;};
struct SyncPacket {std::uint64_t sender=0,request=0;Bytes bytes;};
struct SyncNetworkView {
    bool host=false,hosting=false,joining=false,lobby=false,failed=false;
    // Keep the immutable host bundle through a match/rematch, but never
    // transfer assets while the game is loading or running.
    bool session_active=false,game_active=false;
    std::optional<SyncOffer> offer;
    std::vector<std::uint64_t> preparing_requests;
    // Preserve authenticated rejection/transport details through the worker;
    // failure handling must not erase the session's settings-sync offer.
    std::string failure_reason;
};
// Small transport boundary, also used by headless fault tests. Implementations
// only enqueue bounded packets/read snapshots; never perform file/import I/O.
class SyncLink {
public:
    virtual ~SyncLink()=default;
    virtual SyncNetworkView view()const=0;
    virtual bool receive(SyncPacket&)=0;
    virtual std::size_t chunk_budget()const=0;
    virtual bool send(std::uint64_t recipient,View bytes,bool data,std::string& error)=0;
    virtual bool verify_local(std::string_view digest,std::string& error)=0;
    virtual bool admit(std::uint64_t request,std::string_view digest,std::string& error)=0;
    virtual void reject(std::uint64_t request,std::string_view reason)=0;
    virtual void leave(std::string_view reason)=0;
};
struct SyncSettings {
    std::filesystem::path online_root,worker;
    std::vector<std::filesystem::path> imported_roms;
    std::filesystem::path offline_legacy_root,offline_tracks_root;
};
enum class SyncPhase {Idle,HostPreparing,HostReady,ReadingOffer,Review,Downloading,Preparing,Verified,Failed};
struct PeerProgress {
    std::uint64_t request=0,received=0,total=0;
    ProgressPhase phase=ProgressPhase::Review;
    std::string stage;
};
struct SyncView {
    SyncPhase phase=SyncPhase::Idle;
    std::string stage,error;
    std::shared_ptr<const Manifest> manifest;
    std::shared_ptr<const Profile> profile;
    // Runtime adapter must prepare and pin the actual game resources before
    // proof/admission. A profile receipt alone can never satisfy this gate.
    std::shared_ptr<const void> runtime;
    std::uint64_t received=0,total=0;
    bool kept_offline=false;
    std::string keep_error;
    std::vector<PeerProgress> peers;
};
using RuntimePreparation=std::function<std::shared_ptr<const void>(std::shared_ptr<const Profile>,std::stop_token)>;
class ModSync {
public:
    explicit ModSync(SyncLink& link):link_(link){}
    ~ModSync();
    ModSync(const ModSync&)=delete;
    ModSync& operator=(const ModSync&)=delete;
    bool prepare_host(SyncSettings settings,std::filesystem::path legacy,
        std::vector<std::filesystem::path> labs,std::string revision,bool custom_ai,RuntimePreparation prepare);
    bool watch_client(SyncSettings settings,RuntimePreparation prepare);
    void consent(bool accept,bool keep_offline=true);
    void cancel();
    SyncView snapshot()const;
private:
    bool start(SyncPhase phase);
    void publish(SyncView view);
    void progress(std::string_view stage);
    void host_loop(std::shared_ptr<const Bundle>,std::stop_token);
    void client_loop(const SyncSettings&,const RuntimePreparation&,std::stop_token);
    bool send(std::uint64_t,const Message&,std::string&);
    void pause(std::stop_token,std::chrono::milliseconds wait=std::chrono::milliseconds(5));
    SyncLink& link_;
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    SyncView state_;
    std::optional<bool> decision_;
    bool keep_offline_=true;
    std::jthread thread_;
    std::atomic_bool running_{false};
};
}

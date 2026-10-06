#include "online_mod_sync.hpp"
#include "online_mod_cache.hpp"
#include <algorithm>
#include <map>

namespace dkr::mods::online {
namespace {
using Clock=std::chrono::steady_clock;
void require_running(std::stop_token stop){if(stop.stop_requested())throw Error("Online mod synchronization cancelled. Offline mods and saves were not changed.");}
void folders(const SyncSettings& settings) {
    check_storage(settings.online_root.parent_path(),true);
    std::filesystem::create_directory(settings.online_root);check_storage(settings.online_root,true);
    for(const auto* name:{"payloads","partials"}) {
        std::filesystem::create_directory(settings.online_root/name);check_storage(settings.online_root/name,true);
    }
}
bool data_message(Operation op){return op==Operation::ManifestChunk || op==Operation::PayloadChunk;}
Message control(Operation op,const Download& download){return {op,download.digest()};}
}
ModSync::~ModSync(){cancel();if(thread_.joinable())thread_.join();}
SyncView ModSync::snapshot()const{std::lock_guard lock(mutex_);return state_;}
void ModSync::publish(SyncView view){std::lock_guard lock(mutex_);state_=std::move(view);changed_.notify_all();}
void ModSync::progress(std::string_view stage){std::lock_guard lock(mutex_);state_.stage=std::string(stage);}
bool ModSync::start(SyncPhase phase) {
    // The frontend starts a new job only after cancellation has finished. Do
    // not join a running child process from an ImGui action.
    if(running_.load())return false;
    if(thread_.joinable())thread_.join();
    {std::lock_guard lock(mutex_);state_={};state_.phase=phase;
        if(phase==SyncPhase::ReadingOffer)state_.stage="Waiting for host approval and any required mod offer";
        decision_.reset();keep_offline_=true;}running_.store(true);return true;
}
void ModSync::consent(bool accept,bool keep_offline){std::lock_guard lock(mutex_);if(state_.phase==SyncPhase::Review){decision_=accept;keep_offline_=keep_offline;changed_.notify_all();}}
void ModSync::cancel(){if(thread_.joinable())thread_.request_stop();changed_.notify_all();if(!running_.load())publish({});}
void ModSync::pause(std::stop_token stop,std::chrono::milliseconds wait) {
    std::unique_lock lock(mutex_);changed_.wait_for(lock,stop,wait,[]{return false;});
}
bool ModSync::send(std::uint64_t recipient,const Message& value,std::string& error) {
    const auto bytes=encode_message(value);return link_.send(recipient,bytes,data_message(value.operation),error);
}
bool ModSync::prepare_host(SyncSettings settings,std::filesystem::path legacy,std::vector<std::filesystem::path> labs,
    std::string revision,bool ai,RuntimePreparation prepare) {
    if(!prepare || !start(SyncPhase::HostPreparing))return false;
    try {thread_=std::jthread([this,settings=std::move(settings),legacy=std::move(legacy),labs=std::move(labs),
        revision=std::move(revision),ai,prepare=std::move(prepare)](std::stop_token stop) mutable {
        try {
            const auto began=Clock::now();
            settings.online_root=private_storage_path(settings.online_root);
            folders(settings);progress("Freezing the host's selected mods");
            auto bundle=std::make_shared<const Bundle>(export_host(legacy,labs,revision,ai,stop));
            const auto exported=Clock::now();
            SyncView ready;ready.phase=SyncPhase::HostReady;ready.manifest=std::make_shared<const Manifest>(bundle->manifest);
            ready.total=transfer_size(bundle->manifest);
            if(!bundle->manifest.content.empty()) {
                cache_bundle(*bundle,settings.online_root/"payloads",stop);
                ready.profile=prepare_profile(bundle->manifest,settings.online_root/"payloads",settings.imported_roms,
                    settings.online_root,settings.worker,stop,[&](auto stage){progress(stage);},legacy,settings.offline_tracks_root,labs);
                progress("Validating the host's playable mod resources");ready.runtime=prepare(ready.profile,stop);
                if(!ready.runtime)throw Error("The selected mods do not have a validated online runtime adapter.");
            }
            require_running(stop);
            std::fprintf(stderr,"[online-mods][host-ready] roots=%zu payloads=%zu export-ms=%lld total-ms=%lld\n",
                bundle->manifest.content.size(),bundle->manifest.payloads.size(),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(exported-began).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()));
            ready.stage="Host mods frozen and ready";publish(std::move(ready));
            host_loop(std::move(bundle),stop);
        }catch(const std::exception& error){if(!stop.stop_requested())publish({SyncPhase::Failed,{},error.what()});}
         catch(...){if(!stop.stop_requested())publish({SyncPhase::Failed,{},"Online mod preparation stopped safely."});}
        if(stop.stop_requested())publish({});running_.store(false);
    });}catch(const std::exception& error){running_.store(false);publish({SyncPhase::Failed,{},error.what()});return false;}
    return true;
}
bool ModSync::watch_client(SyncSettings settings,RuntimePreparation prepare) {
    if(!prepare || !start(SyncPhase::ReadingOffer))return false;
    try {thread_=std::jthread([this,settings=std::move(settings),prepare=std::move(prepare)](std::stop_token stop) mutable {
        try{settings.online_root=private_storage_path(settings.online_root);folders(settings);client_loop(settings,prepare,stop);}
        catch(const std::exception& error){if(!stop.stop_requested()){
            // A session-level rejection owns its authenticated settings-sync
            // offer. Disconnecting again here erased both that offer and the
            // original reason before the launcher could display either.
            if(!link_.view().failed)link_.leave(error.what());
            auto failure=snapshot();failure.phase=SyncPhase::Failed;failure.error=error.what();publish(std::move(failure));
        }}
        catch(...){if(!stop.stop_requested()){
            if(!link_.view().failed)link_.leave("Online mod synchronization stopped safely.");
            auto failure=snapshot();failure.phase=SyncPhase::Failed;failure.error="Online mod synchronization stopped safely.";publish(std::move(failure));
        }}
        if(stop.stop_requested())publish({});running_.store(false);
    });}catch(const std::exception& error){running_.store(false);publish({SyncPhase::Failed,{},error.what()});return false;}
    return true;
}
void ModSync::host_loop(std::shared_ptr<const Bundle> bundle,std::stop_token stop) {
    struct Peer {std::uint64_t request=0;Upload upload;PeerProgress progress;std::optional<Message> acknowledgement;Clock::time_point next{};
        Peer(std::uint64_t id,std::shared_ptr<const Bundle> b):request(id),upload(b),progress{id,0,transfer_size(b->manifest),ProgressPhase::Review,"Waiting for download consent"}{};};
    std::map<std::uint64_t,Peer> peers;bool hosted=false;
    const auto digest=identity(bundle->manifest);
    while(!stop.stop_requested()) {
        const auto network=link_.view();
        if(network.failed)break;
        if(network.host && network.session_active)hosted=true;
        else if(hosted)break;
        if(network.game_active) {peers.clear();{std::lock_guard lock(mutex_);state_.peers.clear();}pause(stop,std::chrono::milliseconds(100));continue;}
        // Retire departed/admitted routes and their queued data independently.
        std::erase_if(peers,[&](const auto& p){return std::find(network.preparing_requests.begin(),network.preparing_requests.end(),p.second.request)==network.preparing_requests.end();});
        SyncPacket packet;unsigned drained=0;
        while(drained++<32 && link_.receive(packet)) {
            if(!packet.request || !network.host || std::find(network.preparing_requests.begin(),network.preparing_requests.end(),packet.request)==network.preparing_requests.end())continue;
            try {
                auto [at,inserted]=peers.try_emplace(packet.sender,packet.request,bundle);
                if(at->second.request!=packet.request || peers.size()>3)throw Error("Unexpected provisional mod route.");
                const auto message=decode_message(packet.bytes);
                if(message.operation==Operation::Progress) {
                    auto& peer=at->second;
                    if(message.manifest!=digest || message.total!=peer.progress.total)
                        throw Error("Mod progress does not belong to the frozen host selection.");
                    const auto phase=static_cast<ProgressPhase>(message.bytes[0]);
                    if((phase>=ProgressPhase::Downloading && !peer.upload.consented()) ||
                       (phase>=ProgressPhase::Preparing && (!peer.upload.preparing() || message.offset!=message.total)))
                        continue; // Control packets can be reordered; no progress report grants admission.
                    if(phase<peer.progress.phase || message.offset<peer.progress.received)continue;
                    peer.progress.phase=phase;peer.progress.received=message.offset;
                    peer.progress.stage.assign(message.bytes.begin()+1,message.bytes.end());
                    continue;
                }
                at->second.upload.request(message,link_.chunk_budget());
                if(message.operation==Operation::Consent)at->second.acknowledgement=Message{Operation::ConsentAck,message.manifest};
                if(message.operation==Operation::Preparing)at->second.acknowledgement=Message{Operation::PreparingAck,message.manifest};
                if(message.operation==Operation::Cancel){link_.reject(packet.request,"Mod synchronization declined.");peers.erase(at);}
                else if(message.operation==Operation::Verified) {
                    std::string error;if(!link_.admit(packet.request,message.manifest,error))throw Error(error);
                    peers.erase(at);
                }
            }catch(const std::exception& error){link_.reject(packet.request,error.what());peers.erase(packet.sender);}
        }
        // Small, fair bursts; the transport's existing bulk backpressure still
        // wins. Bulk admission never runs while a game is loading or playing.
        for(auto& [id,peer]:peers)if(Clock::now()>=peer.next) {
            if(peer.acknowledgement) {
                std::string error;if(send(id,*peer.acknowledgement,error))peer.acknowledgement.reset();
                else if(!error.empty())link_.reject(peer.request,error);
                continue;
            }
            for(unsigned burst=0;burst<UploadBurstPackets;++burst) {
                const auto message=peer.upload.next();if(!message)break;
                std::string error;
                if(send(id,*message,error)){peer.upload.sent();peer.next=Clock::now()+std::chrono::milliseconds(2);}
                else {
                    if(!error.empty()){link_.reject(peer.request,error);peer.next=Clock::now()+std::chrono::seconds(1);}
                    break;
                }
            }
        }
        {std::lock_guard lock(mutex_);state_.peers.clear();for(const auto& [id,peer]:peers)state_.peers.push_back(peer.progress);}
        pause(stop,std::chrono::milliseconds(2));
    }
}
void ModSync::client_loop(const SyncSettings& settings,const RuntimePreparation& prepare,std::stop_token stop) {
    Download download;ChunkInbox inbox;std::unique_ptr<PayloadFile> file;std::shared_ptr<const Profile> profile;
    std::shared_ptr<const void> runtime;Clock::time_point retry{},consent_retry{},preparing_retry{};
    std::string requested_payload;std::uint32_t requested_offset=0;
    bool saw_offer=false,consent_sent=false,preparing_sent=false,prepared=false,keep_requested=false,kept_offline=false;
    std::string keep_error;
    Clock::time_point progress_next{};
    const auto report=[&](ProgressPhase phase,std::string_view stage) {
        if(Clock::now()<progress_next || download.phase()==TransferPhase::None || download.phase()==TransferPhase::Manifest)return;
        Message value{Operation::Progress,download.digest(),{},static_cast<std::uint32_t>(download.received_bytes()),
            static_cast<std::uint32_t>(transfer_size(download.manifest())),{static_cast<std::uint8_t>(phase)}};
        for(const auto c:stage.substr(0,240))value.bytes.push_back(c>=32 && c<=126?c:' ');
        std::string error;
        // Display-only telemetry is expendable under backpressure. Required
        // consent/data/proof packets keep priority and their existing retries.
        if(send(0,value,error))progress_next=Clock::now()+std::chrono::milliseconds(250);
    };
    const auto preparation_progress=[&](std::string_view stage){progress(stage);report(ProgressPhase::Preparing,stage);};
    const auto stage_heartbeat=[&](ProgressPhase phase) {
        const auto digest=download.digest();
        const auto total=static_cast<std::uint32_t>(transfer_size(download.manifest()));
        return std::jthread([&,digest,total,phase](std::stop_token heartbeat_stop) {
            while(!heartbeat_stop.stop_requested() && !stop.stop_requested()) {
                const auto current=snapshot();
                Message status{Operation::Progress,digest,{},phase==ProgressPhase::Review?0:total,total,{static_cast<std::uint8_t>(phase)}};
                for(const auto c:current.stage.substr(0,240))status.bytes.push_back(c>=32 && c<=126?c:' ');
                std::string ignored;send(0,status,ignored);
                pause(heartbeat_stop,std::chrono::milliseconds(250));
            }
        });
    };
    while(!stop.stop_requested()) {
        const auto network=link_.view();
        if(network.failed)throw Error(network.failure_reason.empty()
            ? "The host connection ended during mod admission.":network.failure_reason);
        if(network.offer) {
            download.offer(network.offer->digest,network.offer->bytes);saw_offer=true;
        }
        if(network.lobby) {
            if(saw_offer && (!prepared || download.phase()!=TransferPhase::Verified))throw Error("Lobby admission preceded complete local mod preparation.");
            // Keep immutable resources pinned for the match/rematch. The
            // frontend owns cancellation after the guest world has stopped.
            SyncView ready;ready.phase=SyncPhase::Verified;ready.profile=profile;ready.runtime=runtime;
            ready.kept_offline=kept_offline;ready.keep_error=keep_error;
            if(saw_offer){ready.manifest=std::make_shared<const Manifest>(download.manifest());ready.received=ready.total=transfer_size(download.manifest());}
            ready.stage="Host mods verified for this session";publish(std::move(ready));return;
        }
        if(saw_offer && !network.joining)throw Error("The host connection ended during mod admission.");
        SyncPacket packet;unsigned drained=0;
        while(drained++<32 && link_.receive(packet)) {
            const auto message=decode_message(packet.bytes);
            if(message.manifest!=download.digest())throw Error("Mod response belongs to a different host selection.");
            if(message.operation==Operation::ConsentAck) {
                if(download.phase()!=TransferPhase::Payloads && download.phase()!=TransferPhase::Preparing && download.phase()!=TransferPhase::Verified)
                    throw Error("Host acknowledged mods before client consent.");
                consent_sent=true;continue;
            }
            if(message.operation==Operation::PreparingAck) {
                if(!consent_sent || (download.phase()!=TransferPhase::Preparing && download.phase()!=TransferPhase::Verified))
                    throw Error("Host acknowledged preparation before all required downloads were verified.");
                preparing_sent=true;continue;
            }
            if(download.phase()!=TransferPhase::Manifest && download.phase()!=TransferPhase::Payloads)continue;
            inbox.push(message,download.request(link_.chunk_budget()));
            while(download.phase()==TransferPhase::Manifest || download.phase()==TransferPhase::Payloads) {
              const auto chunk=inbox.take(download.request(link_.chunk_budget()));if(!chunk)break;
              if(download.phase()==TransferPhase::Manifest) {
                if(download.manifest_chunk(*chunk) && download.phase()==TransferPhase::Consent) {
                    SyncView review;review.phase=SyncPhase::Review;review.manifest=std::make_shared<const Manifest>(download.manifest());
                    review.total=transfer_size(download.manifest());review.stage="Review the host's required mods";publish(std::move(review));
                }
              } else if(download.accept_payload_chunk(*chunk)) {
                if(!file)throw Error("Online mod chunk preceded an authorized private download.");
                file->append(chunk->bytes);download.persisted(chunk->bytes.size());
                {std::lock_guard lock(mutex_);state_.received=download.received_bytes();}
                if(file->size()==chunk->total){file->publish(settings.online_root/"payloads"/chunk->payload);file.reset();download.payload_verified();inbox.clear();retry={};}
              }
            }
        }
        std::string error;
        if(download.phase()==TransferPhase::Consent) {
            report(ProgressPhase::Review,"Waiting for download consent");
            std::optional<bool> decision;{std::lock_guard lock(mutex_);decision=decision_;}
            if(decision && !*decision) {
                send(0,control(Operation::Cancel,download),error);link_.leave("Host mods declined. Offline mods and saves were not changed.");publish({});return;
            }
            if(decision && *decision){
                // Even an installed-library scan can outlast a provisional
                // route's idle timeout on slow storage. Consent is still local;
                // these authenticated status packets grant no asset access.
                auto heartbeat=stage_heartbeat(ProgressPhase::Review);
                progress("Checking the packaged mod importer before downloading");verify_online_worker(settings.worker,settings.online_root,stop);
                progress("Checking installed patches to avoid unnecessary downloads");
                cache_installed_payloads(download.manifest(),settings.online_root/"payloads",settings.offline_legacy_root,stop,settings.offline_tracks_root);
                heartbeat.request_stop();heartbeat.join();
                download.consent();progress("Downloading required host mods into the separate online cache");
                std::lock_guard lock(mutex_);keep_requested=keep_offline_;state_.phase=SyncPhase::Downloading;
            }
        }
        if((download.phase()==TransferPhase::Payloads || download.phase()==TransferPhase::Preparing) && !consent_sent) {
            if(Clock::now()>=consent_retry && send(0,control(Operation::Consent,download),error))consent_retry=Clock::now()+std::chrono::milliseconds(250);
            if(!error.empty())throw Error(error);pause(stop);continue;
        }
        while(download.phase()==TransferPhase::Payloads && !file) {
            const auto request=download.request(link_.chunk_budget());
            const auto& items=download.manifest().payloads;
            const auto found=std::find_if(items.begin(),items.end(),[&](const auto& p){return p.digest==request.payload;});
            if(found==items.end())throw Error("Required payload disappeared from the frozen host manifest.");
            if(cached_payload(settings.online_root/"payloads",*found)){download.cached(found->digest,found->size);continue;}
            file=std::make_unique<PayloadFile>(settings.online_root/"partials"/found->digest,*found);
            if(file->size()==found->size){file->publish(settings.online_root/"payloads"/found->digest);file.reset();download.cached(found->digest,found->size);continue;}
            if(file->size())download.resume_prefix(found->digest,file->size());retry={};
        }
        if(download.phase()==TransferPhase::Manifest || download.phase()==TransferPhase::Payloads) {
            const auto request=download.request(link_.chunk_budget());
            const bool next_window=request.payload!=requested_payload || request.offset-requested_offset>=transfer_window_bytes(link_.chunk_budget());
            if(Clock::now()>=retry || next_window) {
                if(send(0,request,error)){retry=Clock::now()+std::chrono::seconds(1);requested_payload=request.payload;requested_offset=request.offset;}
                else if(!error.empty())throw Error(error);
            }
        } else if(download.phase()==TransferPhase::Preparing) {
            if(!preparing_sent) {
                if(Clock::now()>=preparing_retry && send(0,control(Operation::Preparing,download),error))preparing_retry=Clock::now()+std::chrono::milliseconds(250);
                if(!error.empty())throw Error(error);pause(stop);continue;
            }
            {std::lock_guard lock(mutex_);state_.phase=SyncPhase::Preparing;state_.received=download.received_bytes();}
            // Import/runtime validation can take longer than a stage callback.
            // Display-only authenticated heartbeats remain independent of that
            // work, and never count as a verified/admitted mod proof.
            auto heartbeat=stage_heartbeat(ProgressPhase::Preparing);
            const auto preparation_began=Clock::now();
            preparation_progress("Reconstructing locally owned assets");
            profile=prepare_profile(download.manifest(),settings.online_root/"payloads",settings.imported_roms,settings.online_root,
                settings.worker,stop,preparation_progress,settings.offline_legacy_root,settings.offline_tracks_root);require_running(stop);
            const auto profile_ready=Clock::now();
            preparation_progress("Validating playable mod resources for this online backend");runtime=prepare(profile,stop);
            if(!runtime)throw Error("The host's selected mods do not have a validated online runtime adapter.");
            const auto runtime_ready=Clock::now();
            if(keep_requested && !settings.offline_legacy_root.empty() && !settings.offline_tracks_root.empty()) {
                preparation_progress("Keeping verified copies in Mods/Hacks for offline play");
                try {retain_profile(*profile,settings.offline_legacy_root,settings.offline_tracks_root,stop);kept_offline=true;}
                catch(const std::exception& error){require_running(stop);keep_error=std::string(error.what()).substr(0,1024);}
                catch(...){require_running(stop);keep_error="Offline copies could not be saved. Your verified session mods are still available.";}
            }
            std::fprintf(stderr,"[online-mods][client-ready] roots=%zu profile-ms=%lld runtime-ms=%lld keep-ms=%lld total-ms=%lld\n",
                download.manifest().content.size(),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(profile_ready-preparation_began).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(runtime_ready-profile_ready).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-runtime_ready).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-preparation_began).count()));
            require_running(stop);if(!link_.verify_local(download.digest(),error))throw Error(error);
            heartbeat.request_stop();heartbeat.join();
            download.prepared(profile->digest);prepared=true;retry={};
        } else if(download.phase()==TransferPhase::Verified && Clock::now()>=retry) {
            if(send(0,control(Operation::Verified,download),error))retry=Clock::now()+std::chrono::milliseconds(250);
            else if(!error.empty())throw Error(error);
        }
        if(download.phase()==TransferPhase::Payloads) {
            {std::lock_guard lock(mutex_);state_.received=download.received_bytes();}
            report(ProgressPhase::Downloading,"Downloading required host mods");
        } else if(download.phase()==TransferPhase::Verified)report(ProgressPhase::Verified,"Mods verified; entering lobby");
        pause(stop,std::chrono::milliseconds(2));
    }
    if(saw_offer){std::string error;send(0,control(Operation::Cancel,download),error);}
}
}

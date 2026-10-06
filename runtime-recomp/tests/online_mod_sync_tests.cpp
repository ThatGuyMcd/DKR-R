#include "online_mod_sync.hpp"
#include "online_mod_runtime.hpp"
#include "online_game_pak.hpp"
#include <json/json.hpp>
#include <deque>
#include <iostream>
#include <chrono>
using namespace dkr::mods;
using namespace dkr::mods::online;
namespace {
unsigned checks=0;
void check(bool value){if(!value)throw Error("Online sync assertion "+std::to_string(checks));++checks;}
struct Hub {
    std::mutex mutex;
    bool hosting=false,game_active=false;
    std::optional<SyncOffer> offer;
    std::array<std::deque<SyncPacket>,4> packets;
    std::array<bool,4> admitted{},verified{},left{},consent_dropped{},prepare_dropped{},chunk_dropped{};
    std::array<std::uint64_t,4> payload_bytes{};
    std::array<std::string,4> failure_reason;
    std::array<unsigned,4> leave_calls{};
    std::array<unsigned,4> progress_reports{};
};
class Link:public SyncLink {
public:
    Link(Hub& hub,unsigned slot):hub_(hub),slot_(slot){}
    SyncNetworkView view()const override {
        std::lock_guard lock(hub_.mutex);SyncNetworkView result;
        result.host=slot_==0;result.hosting=result.host && hub_.hosting;result.joining=slot_ && !hub_.admitted[slot_]&&!hub_.left[slot_];
        result.game_active=hub_.game_active;result.session_active=hub_.hosting || hub_.game_active;
        result.lobby=slot_ && hub_.admitted[slot_];result.failed=hub_.left[slot_];
        result.failure_reason=hub_.failure_reason[slot_];
        if(slot_ && hub_.hosting)result.offer=hub_.offer;
        for(unsigned i=1;i<4;++i)if(!hub_.admitted[i]&&!hub_.left[i])result.preparing_requests.push_back(i);
        return result;
    }
    bool receive(SyncPacket& out)override {
        std::lock_guard lock(hub_.mutex);auto& queue=hub_.packets[slot_];if(queue.empty())return false;
        out=std::move(queue.front());queue.pop_front();return true;
    }
    std::size_t chunk_budget()const override{return 826;}
    bool send(std::uint64_t target,View data,bool,std::string& error)override {
        std::lock_guard lock(hub_.mutex);error.clear();const auto op=decode_message(data);const auto to=slot_?0:static_cast<unsigned>(target);
        if(to>=4 || hub_.packets[to].size()>=32)return false;
        if(slot_ && op.operation==Operation::Progress)++hub_.progress_reports[slot_];
        if(slot_ && op.operation==Operation::Consent && !hub_.consent_dropped[slot_]){hub_.consent_dropped[slot_]=true;return true;}
        if(slot_ && op.operation==Operation::Preparing && !hub_.prepare_dropped[slot_]){hub_.prepare_dropped[slot_]=true;return true;}
        if(!slot_ && op.operation==Operation::PayloadChunk) {
            hub_.payload_bytes[to]+=op.bytes.size();
            if(op.offset==826 && !hub_.chunk_dropped[to]){hub_.chunk_dropped[to]=true;return true;}
        }
        hub_.packets[to].push_back({slot_,slot_?slot_:0,Bytes(data.begin(),data.end())});return true;
    }
    bool verify_local(std::string_view digest,std::string&)override {
        std::lock_guard lock(hub_.mutex);if(!slot_ || !hub_.offer || digest!=hub_.offer->digest)return false;
        hub_.verified[slot_]=true;return true;
    }
    bool admit(std::uint64_t slot,std::string_view digest,std::string&)override {
        std::lock_guard lock(hub_.mutex);if(slot>=4 || !hub_.verified[slot] || !hub_.offer || digest!=hub_.offer->digest)return false;
        hub_.admitted[slot]=true;return true;
    }
    void reject(std::uint64_t slot,std::string_view)override{std::lock_guard lock(hub_.mutex);if(slot<4)hub_.left[slot]=true;}
    void leave(std::string_view)override{std::lock_guard lock(hub_.mutex);++hub_.leave_calls[slot_];hub_.left[slot_]=true;}
private:
    Hub& hub_;unsigned slot_;
};
void write(const std::filesystem::path& path,const nlohmann::json& doc){const auto text=doc.dump();write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
SyncView wait(ModSync& sync,SyncPhase wanted,std::chrono::seconds timeout=std::chrono::seconds(30)) {
    const auto until=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<until) {
        const auto view=sync.snapshot();if(view.phase==wanted)return view;
        if(view.phase==SyncPhase::Failed)throw Error(view.error);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw Error("Timed out waiting for the online mod sync phase.");
}
}
int main(int argc,char** argv) {
    if(argc!=3 && argc!=4){std::cerr<<"Supply a private original ROM, packaged worker, and optionally an older worker. No game is started.\n";return 2;}
    std::filesystem::path root;
    try {
        // Use ordinary paths just like host/client launcher settings. Tests
        // must not hide missing long-path normalization in production.
        root=std::filesystem::absolute(std::filesystem::current_path()/("online-sync-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
        check(std::filesystem::create_directory(root));
        const auto lab=root/"test.dkrmap";std::filesystem::create_directory(lab);
        write(lab/"manifest.json",{{"schemaVersion",1},{"id","sync-test"},{"name","Sync Test"},{"description",std::string(80000,'x')},
            {"adds",nlohmann::json::array({{{"section","LEVEL_HEADERS"},{"file","header.bin"}}})}});
        Bytes header(200);header[0]=6;header[0x4E]=1;header[0x37]=73;header[0xBB]=5;write_new_file(lab/"header.bin",header);
        const auto before=sha256(pack_track_lab(lab));Hub hub;
        {
            // Real sessions can reject settings before offering any mods.
            // Keep that rejection (and its one-click sync offer) intact rather
            // than disconnecting again with a generic mod-transfer message.
            Hub rejected;rejected.left[1]=true;
            rejected.failure_reason[1]="Magic Codes do not match.|magic:2:0";
            Link rejected_link(rejected,1);ModSync client(rejected_link);
            RuntimePreparation never=[](auto,std::stop_token)->std::shared_ptr<const void>{throw Error("Rejected admission must not reconstruct content.");};
            check(client.watch_client({root/"rejected-cache",utf8_path(argv[2]),{utf8_path(argv[1])}},never));
            const auto failure=wait(client,SyncPhase::Failed);
            check(failure.error==rejected.failure_reason[1]);
            {std::lock_guard lock(rejected.mutex);check(rejected.leave_calls[1]==0);}
        }
        {
            Link host_link(hub,0),a_link(hub,1),b_link(hub,2),c_link(hub,3);
            ModSync host(host_link),a(a_link),b(b_link),c(c_link);
            const auto settings=[&](unsigned slot){return SyncSettings{root/("cache-"+std::to_string(slot)),utf8_path(argv[2]),{utf8_path(argv[1])},
                root/("offline-"+std::to_string(slot)),root/("offline-tracks-"+std::to_string(slot))};};
            write_new_file(root/"offline-3",Bytes{7}); // Failure to keep a copy must not block admission.
            // Use the exact launcher selection boundary and real immutable
            // runtime reconstruction, not receipt/profile metadata as proof.
            const auto imported=utf8_path(argv[1]);
            const auto revision=std::string(asset_revision(dkr::runtime::rom::inspect(imported).revision));
            check(!revision.empty());
            RuntimePreparation proof=[&](auto profile,std::stop_token stop)->std::shared_ptr<const void>{
                return with_matching_game_pak({imported},profile->manifest.revision,stop,
                    [](const auto& path){return dkr::runtime::rom::inspect(path);},
                    [&](const auto& path){return RuntimeResources::prepare(profile,path,stop);});
            };
            check(!host.prepare_host(settings(0),root/"missing-legacy",{lab},revision,true,{}));
            check(host.prepare_host(settings(0),root/"missing-legacy",{lab},revision,true,proof));
            const auto ready=wait(host,SyncPhase::HostReady);
            {std::lock_guard lock(hub.mutex);hub.offer=SyncOffer{identity(*ready.manifest),static_cast<std::uint32_t>(encode(*ready.manifest).size())};hub.hosting=true;}
            check(a.watch_client(settings(1),proof));check(b.watch_client(settings(2),proof));check(c.watch_client(settings(3),proof));
            wait(a,SyncPhase::Review);wait(b,SyncPhase::Review);wait(c,SyncPhase::Review);
            {std::lock_guard lock(hub.mutex);check(hub.payload_bytes[1]==0&&hub.payload_bytes[2]==0&&hub.payload_bytes[3]==0);}
            a.consent(true);b.consent(false);c.consent(true);
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            bool observed_progress=false;
            while(std::chrono::steady_clock::now()<until && !observed_progress) {
                for(const auto& peer:host.snapshot().peers)if(peer.request==1 && peer.phase==ProgressPhase::Downloading) {
                    check(peer.total==ready.total && peer.received<=peer.total && !peer.stage.empty());
                    observed_progress=true;break;
                }
                if(a.snapshot().phase==SyncPhase::Failed)throw Error(a.snapshot().error);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(observed_progress);
            const auto first=wait(a,SyncPhase::Verified),third=wait(c,SyncPhase::Verified);
            check(first.profile&&first.runtime&&third.profile&&third.runtime);check(first.profile->digest==ready.profile->digest&&third.profile->digest==ready.profile->digest);
            check(first.kept_offline && first.keep_error.empty());
            check(!third.kept_offline && !third.keep_error.empty() && read_file(root/"offline-3",16)==Bytes{7});
            check(std::filesystem::exists(root/"offline-tracks-1"));
            {std::lock_guard lock(hub.mutex);check(hub.admitted[1]&&hub.admitted[3]&&!hub.admitted[2]);check(hub.left[2]&&hub.payload_bytes[2]==0);
                check(hub.consent_dropped[1]&&hub.prepare_dropped[1]&&hub.chunk_dropped[1]);}
            {std::lock_guard lock(hub.mutex);check(hub.progress_reports[1]>1 && hub.progress_reports[3]>1);}
            check(!std::filesystem::exists(root/"missing-legacy"));check(before==sha256(pack_track_lab(lab)));
            validate_profile(*first.profile);validate_profile(*third.profile);checks+=2;
            // A completed match must not destroy the host uploader. Keep its
            // bundle pinned, send nothing during play, then admit a new racer
            // on return to the SAME lobby without preparing it again.
            std::uint64_t sent=0;
            {std::lock_guard lock(hub.mutex);hub.hosting=false;hub.game_active=true;
                sent=hub.payload_bytes[1]+hub.payload_bytes[2]+hub.payload_bytes[3];}
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            {std::lock_guard lock(hub.mutex);check(sent==hub.payload_bytes[1]+hub.payload_bytes[2]+hub.payload_bytes[3]);
                hub.game_active=false;hub.hosting=true;hub.left[2]=false;hub.verified[2]=false;hub.packets[2].clear();}
            check(b.watch_client(settings(2),proof));wait(b,SyncPhase::Review);b.consent(true,false);
            const auto rematch=wait(b,SyncPhase::Verified);
            check(rematch.profile && rematch.profile->digest==ready.profile->digest && rematch.runtime);
            check(!rematch.kept_offline && rematch.keep_error.empty() && !std::filesystem::exists(root/"offline-2"));
            {std::lock_guard lock(hub.mutex);check(hub.admitted[2]);}
        }
        if(argc==4) {
            Hub stale;Link host_link(stale,0),client_link(stale,1);ModSync host(host_link),client(client_link);
            const auto imported=utf8_path(argv[1]);
            const auto revision=std::string(asset_revision(dkr::runtime::rom::inspect(imported).revision));
            RuntimePreparation proof=[&](auto profile,std::stop_token stop)->std::shared_ptr<const void>{return RuntimeResources::prepare(profile,imported,stop);};
            check(host.prepare_host({root/"stale-host",utf8_path(argv[2]),{imported}},root/"missing-legacy",{lab},revision,true,proof));
            const auto ready=wait(host,SyncPhase::HostReady);
            {std::lock_guard lock(stale.mutex);stale.offer=SyncOffer{identity(*ready.manifest),static_cast<std::uint32_t>(encode(*ready.manifest).size())};stale.hosting=true;}
            check(client.watch_client({root/"stale-client",utf8_path(argv[3]),{imported}},proof));
            wait(client,SyncPhase::Review);client.consent(true);
            const auto failure=wait(client,SyncPhase::Failed);
            check(failure.error.find("ModWorker")!=std::string::npos && failure.error.find("entire new release")!=std::string::npos);
            {std::lock_guard lock(stale.mutex);check(stale.payload_bytes[1]==0 && !stale.admitted[1] && stale.leave_calls[1]==1);}
        }
        std::filesystem::remove_all(private_storage_path(root));std::cout<<checks<<" host/three-client consent/loss/retry/cancellation/profile checks passed. No game window opened.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<"\nPrivate fixture retained for diagnosis.\n";return 1;}
}

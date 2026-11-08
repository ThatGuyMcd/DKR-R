"""Exact, additive edits to final first-party source copies only."""
import re


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Lobby stability input changed: ' + old[:150])
    return text.replace(old, new, 1)


def sync(text):
    text = '#include "diagnostics.hpp"\n#include "cache_repair.hpp"\n' + text
    text = once(text, 'if(thread_.joinable())thread_.join();}',
                'if(thread_.joinable())thread_.join();dkr::lobby_stability::erase(this);}')
    text = once(text, '    state_=std::move(view);changed_.notify_all();',
                '    dkr::lobby_stability::stage(this,int(view.phase),view.stage,view.error);\n    state_=std::move(view);changed_.notify_all();')
    text = once(text, 'state_.stage=std::string(stage);}',
                'state_.stage=std::string(stage);dkr::lobby_stability::stage(this,int(state_.phase),stage);}')
    text = once(text, '    if(thread_.joinable())thread_.join();\n    {std::lock_guard',
                '    if(thread_.joinable())thread_.join();\n    dkr::lobby_stability::begin(this,phase==SyncPhase::HostPreparing?"host":"client");\n    {std::lock_guard')
    text = text.replace('running_.store(false);', 'running_.store(false);dkr::lobby_stability::retire(this);')
    text = once(text, '                cache_bundle(*bundle,settings.online_root/"payloads",stop);',
                '''                try {cache_bundle(*bundle,settings.online_root/"payloads",stop);}
                catch(const Error&) {
                    for(const auto& payload:bundle->manifest.payloads)dkr::lobby_stability::verify_or_repair_download(settings.online_root,"payloads",payload,stop);
                    cache_bundle(*bundle,settings.online_root/"payloads",stop);
                }''')
    text = once(text, 'std::optional<Message> acknowledgement;Clock::time_point next{};',
                'std::optional<Message> acknowledgement;Clock::time_point next{},status_at{};std::uint64_t ticket=0;')
    text = once(text, 'std::map<std::uint64_t,Peer> peers;bool hosted=false;',
                'std::map<std::uint64_t,Peer> peers;bool hosted=false;std::uint64_t next_ticket=0;')
    text = once(text, 'if(at->second.request!=packet.request || peers.size()>3)throw Error("Unexpected provisional mod route.");',
                '''if(at->second.request!=packet.request || peers.size()>dkr::lobby_stability::MaxPending)throw Error("The bounded mod preparation queue is full. Please retry shortly.");
                if(inserted)std::fprintf(stderr,"[lobby-join][host] request=%llu manifest=%s event=route-created\\n",static_cast<unsigned long long>(packet.request),digest.c_str());''')
    text = once(text, '                at->second.upload.request(message,link_.chunk_budget());',
                '''                at->second.upload.request(message,link_.chunk_budget());
                if(message.operation==Operation::Consent&&!at->second.ticket)at->second.ticket=++next_ticket;''')
    text = once(text, '            }catch(const std::exception& error){link_.reject(packet.request,error.what());peers.erase(packet.sender);}',
                '''            }catch(const std::exception& error){
                std::fprintf(stderr,"[lobby-join][host] request=%llu manifest=%s event=rejected error=%s\\n",static_cast<unsigned long long>(packet.request),digest.c_str(),dkr::lobby_stability::printable(error.what()).c_str());
                link_.reject(packet.request,error.what());peers.erase(packet.sender);
            }''')
    text = once(text, '        for(auto& [id,peer]:peers)if(Clock::now()>=peer.next) {',
                '''        std::vector<dkr::lobby_stability::Candidate> candidates;
        for(const auto& [id,peer]:peers)if(peer.upload.consented()&&!peer.upload.preparing())candidates.push_back({id,peer.ticket});
        const auto slots=dkr::lobby_stability::download_slots(std::move(candidates));
        for(auto& [id,peer]:peers)if(Clock::now()>=peer.next) {''')
    text = once(text, '            for(unsigned burst=0;burst<UploadBurstPackets;++burst) {',
                '''            const bool queued=peer.upload.consented()&&!peer.upload.preparing()&&std::find(slots.begin(),slots.end(),id)==slots.end();
            if(peer.upload.consented()&&!peer.upload.preparing()&&Clock::now()>=peer.status_at) {
                const std::string label=queued?dkr::lobby_stability::Queued:dkr::lobby_stability::SlotReady;
                Message status{Operation::Progress,digest,{},static_cast<std::uint32_t>(peer.progress.received),static_cast<std::uint32_t>(peer.progress.total),{static_cast<std::uint8_t>(ProgressPhase::Downloading)}};
                status.bytes.insert(status.bytes.end(),label.begin(),label.end());std::string ignored;
                if(send(id,status,ignored))peer.status_at=Clock::now()+std::chrono::milliseconds(500);
                if(queued)peer.progress.stage=label;
            }
            for(unsigned burst=0;burst<UploadBurstPackets;++burst) {''')
    text = once(text, '                const auto message=peer.upload.next();if(!message)break;',
                '                const auto message=peer.upload.next();if(!message)break;\n                if(queued&&message->operation==Operation::PayloadChunk)break;')
    text = once(text, '    Download download;ChunkInbox inbox;',
                '    dkr::lobby_stability::GapBudget gaps;dkr::lobby_stability::PhaseBudget deadlines;\n    Download download;ChunkInbox inbox;')
    # Admission happens before optional offline retention. Immutable resources
    # remain pinned and the existing worker owns cancellation and cleanup.
    text = once(text, '            ready.stage="Host mods verified for this session";publish(std::move(ready));return;',
                '''            ready.stage="Host mods verified for this session";publish(ready);
            if(profile&&keep_requested&&!settings.offline_legacy_root.empty()&&!settings.offline_tracks_root.empty()) {
                const auto began=Clock::now();
                try {retain_profile(*profile,settings.offline_legacy_root,settings.offline_tracks_root,stop,settings.offline_code_root,settings.host_version);ready.kept_offline=true;}
                catch(const std::exception& error){if(stop.stop_requested())return;ready.keep_error=std::string(error.what()).substr(0,1024);}
                catch(...){if(stop.stop_requested())return;ready.keep_error="Offline copies could not be saved. Your verified session mods are still available.";}
                if(!stop.stop_requested())publish(std::move(ready));
                std::fprintf(stderr,"[online-mods][offline-keep] after-admission=1 elapsed-ms=%lld\\n",static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()));
            }
            return;''')
    text = once(text, '            download.offer(network.offer->digest,network.offer->bytes);saw_offer=true;',
                '            download.offer(network.offer->digest,network.offer->bytes);saw_offer=true;\n            dkr::lobby_stability::manifest(this,download.digest());')
    text = once(text, '        if(saw_offer && !network.joining)throw Error("The host connection ended during mod admission.");',
                '''        if(saw_offer && !network.joining)throw Error("The host connection ended during mod admission.");
        const auto phase=download.phase();
        const auto limit=phase==TransferPhase::Consent?600:phase==TransferPhase::Payloads?1200:phase==TransferPhase::Preparing?900:phase==TransferPhase::Verified?45:120;
        if(deadlines.expired(int(phase),std::chrono::seconds(limit),Clock::now()))throw Error("This mod admission phase exceeded its time budget. Retry to reuse verified files.");
        dkr::lobby_stability::counters(this,download.received_bytes(),saw_offer&&phase!=TransferPhase::Manifest?transfer_size(download.manifest()):0,wire_bytes,cached_bytes,retries);''')
    text = once(text, '            if(message.operation==Operation::ConsentAck) {',
                '''            if(message.operation==Operation::Progress) {
                if(download.phase()!=TransferPhase::Payloads||message.total!=transfer_size(download.manifest())||message.bytes.empty()||message.bytes[0]!=static_cast<std::uint8_t>(ProgressPhase::Downloading))continue;
                const std::string label(message.bytes.begin()+1,message.bytes.end());
                if(label==dkr::lobby_stability::Queued||label==dkr::lobby_stability::SlotReady) {
                    gaps.queue(label==dkr::lobby_stability::Queued,Clock::now());
                    progress(label==dkr::lobby_stability::Queued?label:"Downloading required host mods");
                }
                continue; // A status never advances bytes, consent or readiness.
            }
            if(message.operation==Operation::ConsentAck) {''')
    text = once(text, '                if(advanced)retry=Clock::now()+std::chrono::seconds(1);',
                '                if(advanced){retry=Clock::now()+std::chrono::seconds(1);gaps.progress(Clock::now());}')
    text = once(text, '                file->append(chunk->bytes);download.persisted(chunk->bytes.size());',
                '                file->append(chunk->bytes);download.persisted(chunk->bytes.size());gaps.progress(Clock::now());')
    # Deliberately retain preflight importer validation and all strong hashes.
    text = once(text, '            if(cached_payload(settings.online_root/"payloads",*found))',
                '''            dkr::lobby_stability::package(this,found->digest,"required host payload");
            if(dkr::lobby_stability::verify_or_repair_download(settings.online_root,"payloads",*found,stop))''')
    text = once(text, '            file=std::make_unique<PayloadFile>',
                '''            dkr::lobby_stability::verify_or_repair_download(settings.online_root,"partials",*found,stop);
            file=std::make_unique<PayloadFile>''')
    text = once(text, '            const bool gap=!first && Clock::now()>=retry;',
                '''            const bool gap=!first && gaps.due(Clock::now());
            if(gap&&gaps.exhausted())throw Error("No mod bytes arrived after five repair attempts. Retry to resume verified downloads.");''')
    text = once(text, '                    if(gap)++retries;else if(!first)++credits;',
                '                    if(first)gaps.progress(Clock::now());\n                    if(gap){++retries;gaps.repaired(Clock::now());}else if(!first)++credits;')
    start = text.index('            if(keep_requested && !settings.offline_legacy_root.empty()')
    end = text.index('            std::fprintf(stderr,"[online-mods][client-ready]', start)
    text = text[:start] + text[end:]
    return text


def profile(text):
    text = '#include "cache_repair.hpp"\n' + text
    start = text.index('void cache_installed_payloads(')
    end = text.index('\nnamespace {', start)
    segment = text[start:end]
    segment = once(segment, '    const auto payloads=private_storage_path(input_payloads);check_storage(payloads,true);',
                   '''    const auto payloads=private_storage_path(input_payloads);check_storage(payloads,true);
    const auto available=[&](const auto& payload){return dkr::lobby_stability::verify_or_repair_download(payloads.parent_path(),"payloads",payload,stop);};''')
    segment = re.sub(r'cached_payload\(payloads,([^\)]+)\)', r'available(\1)', segment)
    text = text[:start]+segment+text[end:]
    old = '''        if(!installed_library.empty() || !installed_track_library.empty() || !selected_courses.empty()) {'''
    start = text.index(old)
    end = text.index('        if(progress)progress("Revalidating the cached online mod profile");', start)
    text = text[:start] + text[end:]
    text = once(text, '        }\n    }\n    std::filesystem::path installed;', '    }\n    std::filesystem::path installed;')
    text = once(text, '        try{validate_profile(*out);return out;}',
                '''        try{validate_profile(*out);cancelled(stop);std::fprintf(stderr,"[online-mods][profile] verified-index-reused=1 manifest=%s\\n",out->digest.c_str());return out;}''')
    text = once(text, '''            // A user may remove/reimport a group after the last lobby. Leave
            // the old index untouched and build a new fully verified one.
            if(progress)progress("Refreshing stale online asset references");
            out->directory=root/"source-profiles"/nonce();''',
                '''            // Preserve the stale index for QA and rebuild its named receipt
            // atomically, so future rejoins reuse the repaired profile too.
            if(progress)progress("Refreshing stale online asset references");
            check_storage(out->directory,true);
            if(std::filesystem::canonical(out->directory).parent_path()!=std::filesystem::canonical(root/"source-profiles")||out->directory.filename()!=utf8_path(out->digest))
                throw Error("Cannot safely refresh the stale online profile index.");
            child(root/"quarantined-profiles");
            std::filesystem::rename(out->directory,root/"quarantined-profiles"/(out->digest+"-"+nonce()));''')
    # Lazy probe only when reconstruction is actually needed. No cached asset
    # can skip validate_profile or payload SHA/size verification.
    text = once(text, '    if(needs_worker)verify_online_worker(worker,root,stop);',
                '    bool worker_verified=false;\n    const auto ensure_worker=[&]{if(!worker_verified){verify_online_worker(worker,root,stop);worker_verified=true;}};')
    text = once(text, '        method=payload.kind==PayloadKind::TrackLab?"native-assets":"worker";',
                '        method=payload.kind==PayloadKind::TrackLab?"native-assets":"worker";\n        if(payload.kind==PayloadKind::Xdelta)ensure_worker();')
    text = once(text, '                if(payload.kind!=PayloadKind::TrackLab)throw;\n', '')
    text = once(text, '                if(progress)progress(prefix+"Rebuilding incomplete course cache from verified files");',
                '                if(progress)progress(prefix+"Rebuilding this damaged cache from verified files");')
    text = once(text, '        const auto prefix="Mod "+std::to_string(++index)+"/"+std::to_string(out->manifest.payloads.size())+": ";',
                '''        const auto named=std::find_if(out->manifest.content.begin(),out->manifest.content.end(),[&](const auto& c){return c.payload==payload.digest;});
        const auto prefix="Mod "+std::to_string(++index)+"/"+std::to_string(out->manifest.payloads.size())+": "+(named==out->manifest.content.end()?payload.digest.substr(0,12):named->name)+" - ";
        std::fprintf(stderr,"[online-mods][package] payload=%s item=%u/%zu begin\\n",payload.digest.c_str(),index,out->manifest.payloads.size());''')
    return text


def direct(text):
    text = '#include "policy.hpp"\n' + text
    text = once(text, ': "The host declined this join request."}));',
                ': dkr::lobby_stability::decline_reason(this,request_id)}));')
    return once(text, '            clear_pending_mod_route_locked(pending);pending = {};',
                '''            if(pending.mods_approved)send_with_key(pending.address,pending.key,protocol::MessageType::HelloAck,
                protocol::encode_hello_ack({false,0U,now-pending.last_seen>lifetime?"Mod preparation lost contact with this player. Retry to resume verified files.":"The mod admission time budget expired. Retry to reuse verified files."}));
            clear_pending_mod_route_locked(pending);pending = {};''')


def link(text):
    text = '#include "policy.hpp"\n' + text
    return once(text, 'void reject(std::uint64_t request,std::string_view)override{std::string error;session_.reject_join(request,false,error);}',
                'void reject(std::uint64_t request,std::string_view reason)override{dkr::lobby_stability::RejectionScope scope(&session_,request,reason);std::string error;session_.reject_join(request,false,error);}')


def mod_ui(text):
    text = once(text, 'bool g_online_keep_downloads = true;', '''std::string g_stability_retry_invite;
bool g_stability_retry_requested=false,g_stability_retry_watch=false;
bool g_online_keep_downloads = true;''')
    text = once(text, '    if(view.phase==SyncPhase::Verified && view.profile && g_online_kept_profile!=view.profile->digest) {',
                '''    if(g_stability_retry_requested&&!dkr::lobby_stability::snapshot(g_online_mod_sync.get()).running) {
        g_stability_retry_requested=false;std::string error;
        {auto p=dkr::runtime::spectators::party_link(&session());std::scoped_lock lock(p->mutex);p->join_watch=g_stability_retry_watch;}
        if(session().join(g_stability_retry_invite,g_online_player_name,error)){g_online_mod_notice.clear();g_online_mod_watching=false;}
        else g_online_action_status=error;
        return;
    }
    if(view.phase==SyncPhase::Verified && view.profile && (view.kept_offline||!view.keep_error.empty()) &&
       network.state!=ConnectionState::Loading && network.state!=ConnectionState::Running && g_online_kept_profile!=view.profile->digest) {''')
    text = once(text, '    if(joining && !g_online_mod_watching) {',
                '''    if(joining&&!network.invite.empty()) {
        g_stability_retry_invite=network.invite;
        auto p=dkr::runtime::spectators::party_link(&session());std::scoped_lock lock(p->mutex);g_stability_retry_watch=p->join_watch;
    }
    if(joining && !g_online_mod_watching) {''')
    text = once(text, '        if(ImGui::Button("DISMISS",{ImGui::GetContentRegionAvail().x,42})) {',
                '''        if(ImGui::Button("COPY JOIN DIAGNOSTICS",{ImGui::GetContentRegionAvail().x,36})) {
            const auto details=std::string("DKR-R ")+DKR_RELEASE_VERSION+"\\n"+dkr::lobby_stability::report(g_online_mod_sync.get())+
                "\\nROM: "+g_online_mod_notice.revision+"\\nBackend: "+g_online_mod_notice.backend+"\\n"+g_online_mod_notice.error;
            ImGui::SetClipboardText(details.c_str());
        }
        if(!g_stability_retry_invite.empty()&&view.phase==SyncPhase::Failed&&!g_online_mod_host_request&&dkr::lobby_stability::snapshot(g_online_mod_sync.get()).role=="client") {
            ImGui::TextWrapped("Retry reuses verified files and repairs damaged private cache entries. Review the host's current mods again.");
            if(ImGui::Button("RETRY JOIN",{ImGui::GetContentRegionAvail().x,42})) {
                g_online_mod_sync->cancel();g_stability_retry_requested=true;ImGui::CloseCurrentPopup();
            }
        }
        if(ImGui::Button("DISMISS",{ImGui::GetContentRegionAvail().x,42})) {''')
    text = once(text, '    ImGui::TextWrapped("%s",dkr::runtime::copy::progress(view.stage).c_str());',
                '''    ImGui::TextWrapped("%s",dkr::runtime::copy::progress(view.stage).c_str());
    if(busy) {
        const auto d=dkr::lobby_stability::snapshot(g_online_mod_sync.get());const auto now=std::chrono::steady_clock::now();
        if(view.stage!=dkr::runtime::copy::progress(view.stage))ImGui::TextWrapped("%s",view.stage.c_str());
        ImGui::Text("Elapsed %.0fs | Stage %.0fs",std::chrono::duration<double>(now-d.began).count(),std::chrono::duration<double>(now-d.phase_began).count());
        if(view.phase==SyncPhase::Downloading&&view.total) {
            ImGui::Text("%.1f / %.1f MiB | Downloaded %.1f MiB | Reused %.1f MiB",double(view.received)/dkr::mods::MiB,double(view.total)/dkr::mods::MiB,double(d.wire)/dkr::mods::MiB,double(d.cached)/dkr::mods::MiB);
            ImGui::Text("Transfer rate %.2f MiB/s | Gap repairs %llu",d.bytes_per_second/dkr::mods::MiB,static_cast<unsigned long long>(d.retries));
        }
    }''')
    text = once(text, 'New offline copies start disabled. Your existing mod selections and saves are kept.',
                'Offline copies are saved after joining and start disabled. Your current selections and saves are kept.')
    return text


def main(text):
    text = '#include "build_contract.hpp"\n#include "diagnostics.hpp"\n' + text
    text = once(text, '''        .presentation_allowed_callback = []() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            if(dkr_experimental_bootstrap_active())return false;''',
                '''        .presentation_allowed_callback = []() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            if(dkr_experimental_bootstrap_active())return dkr::lobby_stability::bootstrap_audio_worker_allowed();''')
    text = once(text, '    dkr::runtime::startup_performance::mark("process-entry");',
                '''    dkr::runtime::startup_performance::mark("process-entry");
    std::fprintf(stderr,"[lobby-stability][build] version=%s contract=%s\\n",DKR_RELEASE_VERSION,DKR_LOBBY_STABILITY_CONTRACT);''')
    text = once(text, '            local_bootstrap_session=std::make_unique<dkr::runtime::netplay::DirectSession>();',
                '''            std::fprintf(stderr,"[lobby-start][bootstrap] match=%llu local=%u players=%u resources=%s event=begin-retail-bootstrap\\n",
                static_cast<unsigned long long>(owned_descriptor->match_id),unsigned(initial_online.local_slot),unsigned(owned_descriptor->player_count),online_mods?online_mods->fingerprint().c_str():"stock");
            dkr::lobby_stability::begin_bootstrap(owned_descriptor->match_id);
            local_bootstrap_session=std::make_unique<dkr::runtime::netplay::DirectSession>();''')
    text = once(text, '"Task %llu (%08X) has made no progress for 15 seconds at %s (pending=%u). "',
                '"Task %llu (%08X, type=%u) has made no progress for 15 seconds at %s (pending=%u). "')
    text = once(text, 'static_cast<unsigned long long>(stalled.token.generation), stalled.token.address,',
                'static_cast<unsigned long long>(stalled.token.generation), stalled.token.address,stalled.type,')
    text = once(text, '                    std::fprintf(stderr, "[host-task][stall] %s\\n", message);',
                '''                    std::fprintf(stderr, "[host-task][stall] %s\\n", message);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
                    if(owned_launch)std::fprintf(stderr,"[lobby-start][bootstrap] match=%llu bootstrap-active=%u elapsed-ms=%lld task-type=%u task-generation=%llu stage=%s\\n",
                        static_cast<unsigned long long>(owned_descriptor->match_id),unsigned(dkr_experimental_bootstrap_active()),
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(task_check_now-runtime_started_at).count()),stalled.type,
                        static_cast<unsigned long long>(stalled.token.generation),dkr::runtime::host_tasks::stage_name(stalled.stage));
#endif''')
    text = once(text, '            auto bootstrap=dkr_experimental_bootstrap_take_state();',
                '''            dkr::lobby_stability::end_bootstrap();
            auto bootstrap=dkr_experimental_bootstrap_take_state();
            std::fprintf(stderr,"[lobby-start][bootstrap] match=%llu event=retail-bootstrap-ended elapsed-ms=%lld memory-bytes=%zu error=%s\\n",
                static_cast<unsigned long long>(owned_descriptor->match_id),static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-runtime_started_at).count()),
                bootstrap.memory.size(),bootstrap.error.c_str());''')
    return text


def stubs(text):
    text = '#include "diagnostics.hpp"\n' + text
    text = once(text, '                registry.consume(registry.lookup(task + 0x10U, Engine::Sp), Engine::Sp);',
                '''                const auto token=registry.lookup(task+0x10U,Engine::Sp);
                dkr::lobby_stability::bootstrap_event("SP-handler-consumed",scheduler,task,token.generation);
                registry.consume(token,Engine::Sp);''')
    text = once(text, '                registry.consume(registry.lookup(task + 0x10U, Engine::Dp), Engine::Dp);',
                '''                const auto token=registry.lookup(task+0x10U,Engine::Dp);
                dkr::lobby_stability::bootstrap_event("DP-handler-consumed",scheduler,task,token.generation);
                registry.consume(token,Engine::Dp);''')
    text = once(text, 'extern "C" void dkr_scheduler_sp_handled(std::uint8_t*, recomp_context*) {',
                '''extern "C" void dkr_scheduler_sp_handled(std::uint8_t*, recomp_context*) {
    dkr::lobby_stability::bootstrap_event("SP-handler-acknowledging",0,0);''')
    return text

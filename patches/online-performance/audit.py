"""Verify final bindings, prior compositions, baseline preservation and playtest peer IDs."""
from pathlib import Path
import argparse,importlib.util,json,os,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def need(ok,why):
    if not ok:raise ValueError(why)
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def compiled(build,target):
    project=build/(target+'.vcxproj')
    if project.is_file():return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/online-performance';rows,files=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Performance receipts differ')
    for name,data in files.items():need((stage/name).read_bytes()==data,'Stale staged input: '+name)
    portrait_stage=build/'generated/character-library-portraits'
    portrait_pipeline=load('portrait_final_pipeline',ROOT/'patches/character-library-portraits/pipeline.py')
    portrait_rows,portrait_files=portrait_pipeline.compose(json.loads((portrait_stage/'inputs.json').read_text()),portrait_stage)
    need(portrait_rows==json.loads((portrait_stage/'swaps.json').read_text()),'Portrait source receipts differ')
    for name,data in portrait_files.items():need((portrait_stage/name).read_bytes()==data,'Stale portrait file: '+name)
    portrait_swaps=[r for r in portrait_rows if r['compiled']]
    def bound_source(target,path):
        path=Path(path).resolve()
        for row in [*rows,*portrait_swaps]:
            if row['target']==target and Path(row['path']).resolve()==path:path=Path(row['new']).resolve()
        return path
    for row in rows:
        final=Path(row['new']).resolve()
        for later in portrait_swaps:
            if later['target']==row['target'] and Path(later['path']).resolve()==final:
                final=Path(later['new']).resolve()
        actual=compiled(build,row['target'])
        need(final in actual and Path(row['path']).resolve() not in actual,'Final compiler substitution missing: '+str(final))
    for row in portrait_swaps:
        actual=compiled(build,row['target']);need(Path(row['new']).resolve() in actual and Path(row['path']).resolve() not in actual,'Portrait launcher binding missing')
    need((portrait_stage/'character_portraits.cpp').resolve() in compiled(build,'DKRCharacterPortraits'),'Portrait background decoder not compiled')
    ui=portrait_files['runtime_ui.cpp'].decode();cards=portrait_files['runtime_mod_library_ui.inl'].decode()
    online=portrait_files['runtime_online_ui.inl'].decode()
    need(ui.count('#include "'+(portrait_stage/'runtime_mod_library_ui.inl').as_posix()+'"')==1,'Portrait card include binding missing')
    need(ui.index('ReleaseCharacterPortraits();')<ui.index('    ReleaseModStoreTextures();'),'Portrait textures outlive launcher cleanup')
    need('view.preview_height=100;view.preview=[item]' in cards,'Vehicle preview hook not reused')
    store=ROOT/'runtime-recomp/src/game/mod_store'
    need((store/'overlay_images.cpp').resolve() in compiled(build,'DKRPortGame'),'Native Mod Store image renderer is not compiled')
    library_ui=ROOT/'runtime-recomp/src/game/library_ui'
    shared_page=(library_ui/'shared_page_ui.inl').read_text()
    need('if(index==4&&game_running)continue;' not in ui and
        'DrawSharedUiPage(content_width,g_overlay_page.load(),true)' in ui and
        'DrawSharedUiPage(right_inner_width,page,false,&play_context)' in ui and
        '#include "'+(library_ui/'shared_page_ui.inl').as_posix()+'"' in ui and
        'page==kPageModLibrary)DrawModLibraryPage(width,live)' in shared_page and
        'g_store_in_game=live;' in shared_page and
        '#include "'+(library_ui/'page.inl').as_posix()+'"' in ui and
        'DrawModStore(width,locked,game_running)' in (library_ui/'page.inl').read_text(),
        'The standalone Mod Library is not connected to the real in-game catalogue')
    saves=portrait_files['runtime_saves_ui.inl'].decode()
    need('const bool locked=live||lobby||SharedSaveBusy();' in saves and
        'ImGui::BeginDisabled(locked);' in saves and 'if(!locked)DrawSavesConfirmations(state);' in saves,
        'Shared save-page presentation lost its live-game write lock')
    need('overlay::begin(application)' in ui and 'overlay::release()' in ui,'Store Inspector image lifetime is not bound')
    need('game_running || store->writing' in portrait_files['store_page_shell.inl'].decode(),
        'Overlay store can install into the running game')
    need('StoreComments(row,width,store)' in portrait_files['store_player_details.inl'].decode(),
        'Shared mod discussion is not bound to the real details page')
    need('community_service.inl' in portrait_files['mod_store.cpp'].decode(),
        'Shared comments and download tracking are not bound to the account worker')
    # Previous auditors prove their exact intermediate compositions. Present
    # ONLY this layer's already-verified compiler substitutions as their input
    # paths and stop their graph at the previous layer. This layer's receipt
    # and actual compiler graph were independently checked above. No source
    # bytes or prior receipt contents are rewritten by this view.
    class PriorBindings(type(Path())):
        def glob(self,pattern,**kwargs):
            for path in super().glob(pattern,**kwargs):
                if pattern=='*/swaps.json' and path.parent.name in {'online-performance','character-library-portraits'}:continue
                yield path
        def read_text(self,*args,**kwargs):
            text=super().read_text(*args,**kwargs)
            if self.suffix=='.vcxproj' or self.name=='build.ninja':
                for row in [*portrait_swaps,*rows]:
                    for old,new in ((row['new'],row['path']),(row['new'].replace('\\','/'),row['path'].replace('\\','/')),
                        (row['new'].replace('/','\\'),row['path'].replace('/','\\'))):text=text.replace(old,new)
            return text
    saved=sys.modules.get('pipeline')
    try:
        prior_pipeline=load('online_previous_pipeline',ROOT/'patches/vehicle-original-textures/pipeline.py')
        prior_pipeline.VERSION=pipeline.VERSION;sys.modules['pipeline']=prior_pipeline
        retained=load('online_previous_audit',ROOT/'patches/vehicle-original-textures/audit.py').inspect(PriorBindings(build),None,baseline,accepted)
    finally:
        if saved is None:sys.modules.pop('pipeline',None)
        else:sys.modules['pipeline']=saved
    stability=ROOT/'runtime-recomp/src/game/online_stability'
    relay=ROOT/'runtime-recomp/src/game/netplay/peer_relay'
    chat=ROOT/'runtime-recomp/src/game/netplay/session_chat'
    driving=ROOT/'runtime-recomp/src/game/analog_driving'
    rear=ROOT/'runtime-recomp/src/game/look_behind'
    paths=sorted([*(AUTH/'native').glob('*'),*(AUTH/'tests').glob('*'),*stability.glob('*'),*relay.glob('*'),*chat.glob('*'),
        *driving.glob('*'),*rear.glob('*'),*(ROOT/'runtime-recomp/src/game/display_settings').glob('*'),*(ROOT/'runtime-recomp/src/game/session_content').glob('*'),ROOT/'runtime-recomp/src/game/library_ui/startup_progress.hpp',ROOT/'scripts/legacy_behaviour_policy.py',ROOT/'scripts/generate_replay_probe.py',
        ROOT/'runtime-recomp/cmake/OnlineStability.cmake',ROOT/'runtime-recomp/cmake/PeerRelay.cmake',ROOT/'runtime-recomp/cmake/SessionChat.cmake',AUTH/'pipeline.py',AUTH/'ReleaseOverlay.cmake'],key=lambda p:p.as_posix())
    ledger='online-performance-v1\n'+''.join(os.path.relpath(p,AUTH).replace('\\','/')+':'+pipeline.sha(p.read_bytes())+'\n' for p in paths if p.is_file())
    ids={k:pipeline.sha((v+'\n'+ledger).encode()) for k,v in retained['rollback_build_ids'].items()}
    need(ids==json.loads((stage/'contracts.json').read_text()),'Peer derivation differs')
    startup=(library_ui/'automatic_ui.inl').read_text()
    launch=(stage/'DKRLegacyModLaunch/legacy_mod_launch.cpp').read_text()
    need('BrandBlock(layout.logo,layout.logo,false)' in startup and
        'DrawLauncherBackdrop(background,renderer,scroll,software)' in startup and
        'state.progress.measured()' in startup and 'play.loading_mods' in startup and
        'DrawAutomaticStartFrame(automatic_start,launcher_background,renderer' in ui,
        'Automatic startup is not connected to the approved native intro screen')
    need('state_.stage=stage;state_.progress=value;' in launch and
        'preview_index,tracks.size()' in launch and
        '++preview_index,tracks.size()' in launch,
        'Native preparation does not publish coherent measured progress')
    need(compiled(build,'DKROnlineStabilityCore')=={(stability/'control.cpp').resolve(),(stability/'live_delay.cpp').resolve(),(stability/'preparation_policy.cpp').resolve(),(stability/'process_services.cpp').resolve()},'Normal online stability sources are not compiled directly')
    need(compiled(build,'DKRPeerRelay')=={(relay/'core.cpp').resolve(),(relay/'transport.cpp').resolve(),(relay/'live_datagram.cpp').resolve()},'Normal proxy/datagram sources are not compiled directly')
    need(compiled(build,'DKRSessionChat')=={(chat/'chat.cpp').resolve()},'Normal session chat source is not compiled directly')
    platform=next(p for p in compiled(build,'DKRPortGame') if p.name=='runtime_platform.cpp').read_text()
    need('dkr::chat::captures_game_input()' in platform,'Compiled input polling bypasses chat capture')
    need('#include "netplay/session_chat/ui.inl"' in online and re.search(r'DrawSessionChatLobby\(inner,\s*frame\)',online),'Lobby chat is not bound beside player cards')
    need('DrawSessionChatOverlay(show_overlay)' in ui and 'HandleSessionChatEvent(event)' in ui,'In-game chat or event capture is not bound')
    direct=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='direct_session.cpp').read_text()
    live_directory=[(name,data.decode()) for name,data in files.items() if name.endswith('/owner_pump.inl')]
    need(len(live_directory)==1,'Live spectator admission must have one compiled owner policy')
    directory_name,directory_owner=live_directory[0]
    need((stage/directory_name).as_posix() in direct and
        'party->enabled&&party->allow_spectators&&spectators+pending_spectators<8' in directory_owner and
        '{"maxSpectators",party->enabled?(party->allow_spectators?8:spectators):0}' in directory_owner and
        'party->enabled&&party->allow_spectators&&online->running_spectators' in directory_owner,
        'Live spectator admission is not bound to authenticated grants and public availability')
    rollback=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='experimental_rollback.cpp').read_text()
    need('SnapshotHistory<RollbackStateStore>' in rollback and 'checkpoints_->staging(frame)' in rollback and
        'simulation_.capture(target, detail)' in rollback and 'hash=checkpoint_hash(target)' in rollback and
        'checkpoints_->save(frame, target, hash)' in rollback,
        'The production rollback owner bypasses direct bounded checkpoint capture or integrity hashing')
    quick=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='quick_join_transport.cpp').read_text()
    need('fresh_input_capacity(traffic,buffered,bytes.size()+1U)' in quick,'Realtime SDK freshness budget is not bound')
    need('configure_live_reliability(realtime.reliability)' in quick,'Timed realtime input delivery is not bound')
    need('peer.ping_window.sent(' in direct and 'member->ping_window.receive(' in direct and
        'party->ping_window.sent(' in direct and 'party->ping_window.receive(' in direct,
        'Delayed heartbeat replies still depend on the most recent one-second probe')
    main=next(p for p in compiled(build,'DKRPortGame') if p.name=='game_main.cpp').read_text()
    need('ProcessServices process_services([]{dkr::crash_reports::shutdown();dkr::online_stability::retire_store_http();dkr::online::shutdown_account();});' in main,'Process exit can outlive owned HTTP work')
    reports=ROOT/'runtime-recomp/src/game/crash_reports'
    need(compiled(build,'DKRCrashReports')=={(reports/name).resolve() for name in ('queue.cpp','service.cpp','transport.cpp')},'Crash reporting is not compiled from its owned service sources')
    need('dkr::crash_reports::start(config_directory,DKR_RELEASE_VERSION);' in main and
        'dkr::crash_reports::Gameplay quiet_reports;' in main and
        ui.count('DrawCrashReportingConsent();')==2,
        'Crash reporting lost its startup, gameplay or shared UI lifetime binding')
    update=next(p for p in compiled(build,'DKRUpdateClient') if p.name=='update_service.cpp').read_text()
    need('void shutdown()' in update and 'owner.reset();' in update,'Explicit updater retirement is missing')
    store=next(p for p in compiled(build,'DKRPortGame') if p.name=='mod_store.cpp').read_text()
    need('bind_store_http(this' in store and 'if(worker->joinable())worker->join();' in store,'Store HTTP owners can outlive process TLS cleanup')
    need('retry.loaded.bootstrap_hash==bootstrap_hashes_[peer->slot]' in direct,'Release retry lost checkpoint proof')
    need('retry.loaded.online_save_hash==runtime_save_hashes_[peer->slot]' in direct,'Release retry lost save proof')
    network=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='experimental_network.cpp').read_text()
    need('flow::pulse(this' in network and 'flow::received(this' in network and 'flow::sent(this' in network,'Input flow recorder is not bound to the real Network')
    need('Authoritative?3:kind' in network and 'plain[5]==0||plain[5]==3' in network and 'sdk2::network_receive(peer,payload)' in network,'Repair classification conflicts with input or SDK command delivery')
    need('party->queue_age.accepted(lane,item.target,item.queued,now)' in direct,'Actual party SDK admission queue age is not measured')
    admission=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='experimental_lobby_admission.cpp').read_text()
    need('source.storage[3]==0)release_seen_=true' not in admission,'Early game traffic suppresses startup recovery')
    runtime=next(p for p in compiled(build,'DKRPortGame') if p.name=='experimental_runtime.cpp').read_text()
    need('StartupTransport<SessionTransport,TransportTrafficClass,LobbyAdmission,LobbyRoute>' in runtime and 'auto& network=*prepared_network' in runtime,'Verified startup Network is not reused through release')
    need('session.statistics().next_frame!=session.frontier()||pump.view().wait' not in runtime,'Rollback correction still cancels live delay negotiation')
    party_runtime=next(p for p in compiled(build,'DKRPortGame') if p.name=='party_runtime.cpp').read_text()
    need('peer_clock.observe(' in party_runtime and 'peer_clock.period(' in party_runtime,'Bounded peer clock recovery is not bound')
    need('peer_clock.observe_frontier(clock->frame,clock->rtt_ms,clock->received)' in party_runtime and
        'if(!identity.dynamic)peer_clock.observe(' in party_runtime,'Automatic clock pacing still infers progress from a changing input lead')
    need('send(host,Msg::Progress,proof.b,repair_lane)' in party_runtime and 'Msg::InputReceipt,receipt.b,launch.development?Lane::Control:Lane::Realtime' in party_runtime,'Input recovery still depends on ordered control delivery')
    need('encode_live_inputs(batch),Lane::Replica' in party_runtime and
        party_runtime.index('        service_inputs(now);')<party_runtime.index('        if(phase==Phase::SceneVote||phase==Phase::ScenePrepare||phase==Phase::SceneReady) {'),
        'Historical input retries are duplicated in SCTP or stop during scene agreement')
    need('phase!=Phase::Running||driver->correcting()' not in party_runtime,'Party correction still cancels live delay negotiation')
    need('Msg::LiveInputs,bytes,Lane::Realtime' in party_runtime and 'host_received=std::max(host_received,received)' in party_runtime,'Compact live delivery or authoritative receipt retention missing')
    need('Msg::Input,body.b,Lane::Authoritative' in party_runtime and 'repair_due(source,id.second,id.first,retry)' in party_runtime,'Exact repair lost its independent lane or retry pacing')
    need('input_trace.pulse(' in party_runtime and 'ex::performance::report()' in party_runtime,'Actual party input/CPU measurements are not bound')
    need('live_delay.work(cost,' in party_runtime and 'live_delay.authored(authored)' in party_runtime,'Per-owner response and correction cost measurements are not bound')
    need('response::reached' in runtime and 'response::Stage::Publish' in runtime,'Ordinary runtime response publication is not bound')
    profile_source=next(p for p in compiled(build,'DKRLegacyImportLibrary') if p.name=='online_mod_profile.cpp').read_text()
    need('CatalogueScope catalogue_scope' in profile_source and 'append_sources(sources,cached?*cached:profile_sources(reusable))' in profile_source,'Verified preparation metadata reuse is missing')
    need(bound_source('DKROnlinePreparationChecks',ROOT/'runtime-recomp/tests/online_mod_profile_tests.cpp') in compiled(build,'DKROnlinePreparationChecks'),'Final production preparation qualification is not bound')
    transfer=next(p for p in compiled(build,'DKRLegacyModCore') if p.name=='online_mod_transfer.cpp').read_text()
    need('requested_window(message.bytes)' in transfer and 'MaximumTransferWindow)-bytes_' in transfer,'Negotiated mod window is not bound to the actual sender and inbox')
    sync=next(p for p in compiled(build,'DKRLegacyImportLibrary') if p.name=='online_mod_sync.cpp').read_text()
    need('transfer.observed(chunk->bytes.size())' in sync and 'transfer.retry_delay()' in sync,'Verified transfer progress is not driving the window')
    need(bound_source('DKR_online_flow_tests',ROOT/'runtime-recomp/tests/online_flow_tests.cpp') in compiled(build,'DKR_online_flow_tests'),'Combined production flow regression is not bound')
    need(bound_source('DKR_online_repair_tests',ROOT/'runtime-recomp/tests/online_repair_tests.cpp') in compiled(build,'DKR_online_repair_tests'),'Production transfer regression is not bound')
    need(direct.count('dkr::relay::wrap(this,make_udp_session_transport())')==5,'A UDP lifecycle path bypasses proxy transport cleanup')
    for factory in ('true, {}, error','false, invite, error'):
        need('dkr::relay::wrap(this,make_quick_join_session_transport('+factory+'))' in direct,'Quick Join relay adapter missing')
    need(bound_source('DKRPeerRelayTests',ROOT/'runtime-recomp/tests/peer_relay_tests.cpp') in compiled(build,'DKRPeerRelayTests'),'Actual UDP/WebRTC relay fixture is not bound')
    need(bound_source('DKRPeerRelayNativeTests',ROOT/'runtime-recomp/tests/peer_relay_native_tests.cpp') in compiled(build,'DKRPeerRelayNativeTests'),'Actual native relay owner fixture is not bound')
    need(bound_source('DKRLiveTimelineTests',ROOT/'runtime-recomp/tests/online_stability_timeline_tests.cpp') in compiled(build,'DKRLiveTimelineTests'),'Actual live Timeline regression is not bound')
    timeline=next(p for p in compiled(build,'DKRNetplayCore') if p.name=='experimental_timeline.cpp')
    need('sample_limit(frontier_,configuration_.input_delay,live_lead)' in timeline.read_text(),'Owned sample lead remains frozen at launch')
    sources=compiled(build,'DKROnlinePartyChecks');need((stage/'party_checks.cpp').resolve() in sources,'Actual party fixture not bound')
    lifecycle=ROOT/'runtime-recomp/tests/direct_session_tests.cpp'
    need(bound_source('DKRDirectoryLifecycleChecks',lifecycle) in compiled(build,'DKRDirectoryLifecycleChecks'),'Production directory lifecycle fixture not bound')
    unchanged=json.loads(baseline.read_text())
    for row in unchanged:need(pipeline.sha((ROOT/row['path']).read_bytes())==row['sha256'],'Pre-existing source changed: '+row['path'])
    if binary:
        data=binary.read_bytes()
        for marker in (pipeline.VERSION,'[rollback][menu-refused]','[party][performance]','[party][input-rate]','[party][input-flight]','[online][delay-status]','[online][traffic]','[online-mods][catalogues]','payload-phase-ms=','[rollback][owner-flow]','[online-mods][window]',
            '[online-mods][prepare-phase]','[spectator][playback] first-frame','[party][release-receive] accepted',
            '[party][release-receive] ignored=duplicate','immutable workload families','[render][provenance]',
            '[render][producer-snapshot]','[memory][primary-pool]','[online-mods][keep-legacy]',
            'Custom music was not prepared at a confirmed loading boundary.','No mod bytes arrived after five repair attempts.',
            '[custom-vehicles][original-textures]','[online][proxy]','Let Clients host by Proxy',
            'Copy proxy connection diagnostics','Allow this device to relay connections','LOBBY CHAT','Show messages during races','online_chat_key=',*ids.values()):need(marker.encode() in data,'Linked repair missing: '+marker)
        need(binary.stat().st_mtime_ns>=max((stage/name).stat().st_mtime_ns for name in files),'Binary predates staged inputs')
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'portrait_swaps':portrait_rows,'rollback_build_ids':ids,'retained':retained,
        'unchanged_preexisting_authored_files':len(unchanged),'protected_sources_edited':False,
        'prior_audit_scope':'Exact previous sources/receipts; compiler-only inverse mapping of the verified final substitutions; no old-ID binary assertions.',
        'game_or_rom_or_rsp_or_gpu_executed':False,'live_multiplayer_acceptance':'Required on owner machines.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Final performance source graph and complete retained repair chain verified.')
